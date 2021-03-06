/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

Multiprotocol is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Multiprotocol.  If not, see <http://www.gnu.org/licenses/>.
 */
// Compatible with GD005 C-17 and GD006 DA62 planes.

#if defined(GD00X_NRF24L01_INO)

#include "iface_nrf24l01.h"

//#define FORCE_GD00X_ORIGINAL_ID

#define GD00X_INITIAL_WAIT    500
#define GD00X_PACKET_PERIOD   3500
#define GD00X_RF_BIND_CHANNEL 2
#define GD00X_PAYLOAD_SIZE    15
#define GD00X_BIND_COUNT	  857	//3sec

#define GD00X_V2_BIND_PACKET_PERIOD	5110
#define GD00X_V2_RF_BIND_CHANNEL	0x43
#define GD00X_V2_PAYLOAD_SIZE		6

// flags going to packet[11]
#define	GD00X_FLAG_DR		0x08
#define	GD00X_FLAG_LIGHT	0x04

// flags going to packet[4]
#define	GD00X_V2_FLAG_DR	0x40
#define	GD00X_V2_FLAG_LIGHT	0x80

static void __attribute__((unused)) GD00X_send_packet()
{
	static uint8_t prev_CH6=false;

	if(sub_protocol==GD_V1)
	{
		packet[0] = IS_BIND_IN_PROGRESS?0xAA:0x55;
		memcpy(packet+1,rx_tx_addr,4);
		uint16_t channel=convert_channel_ppm(AILERON);
		packet[5 ] = channel;
		packet[6 ] = channel>>8;
		channel=convert_channel_ppm(THROTTLE);
		packet[7 ] = channel;
		packet[8 ] = channel>>8;
		channel=convert_channel_ppm(CH5);		// TRIM
		packet[9 ] = channel;
		packet[10] = channel>>8;
		packet[11] = GD00X_FLAG_DR				// Force high rate
				   | GET_FLAG(CH6_SW, GD00X_FLAG_LIGHT);
		packet[12] = 0x00;
		packet[13] = 0x00;
		packet[14] = 0x00;
	}
	else
	{//GD_V2
		if(IS_BIND_IN_PROGRESS)
			for(uint8_t i=0; i<5;i++)
				packet[i]=rx_tx_addr[i];
		else
		{
			packet[0]=convert_channel_16b_limit(THROTTLE,0,100);	// 0..100

			// Deadband is needed on aileron
			uint16_t aileron=limit_channel_100(AILERON);			// 204<->1844
			#define GD00X_V2_DB_MIN 1024-40
			#define GD00X_V2_DB_MAX 1024+40
			if(aileron>GD00X_V2_DB_MIN && aileron<GD00X_V2_DB_MAX)
				packet[1]=0x20;	// Send the channel centered
			else // Ail:  0x3F..0x20..0x00
				if(aileron>GD00X_V2_DB_MAX)
					packet[1]=0x1F-((aileron-GD00X_V2_DB_MAX)*(0x20)/(CHANNEL_MAX_100+1-GD00X_V2_DB_MAX));	// 1F..00
				else
					packet[1]=0x3F-((aileron-CHANNEL_MIN_100)*(0x1F)/(GD00X_V2_DB_MIN-CHANNEL_MIN_100));	// 3F..21

			// Trims must be in a seperate channel for this model
			packet[2]=0x3F-(convert_channel_8b(CH5)>>2);			// Trim: 0x3F..0x20..0x00

			uint8_t seq=((packet_count*3)/7)%5;
			packet[4]=seq | GD00X_V2_FLAG_DR;

			if(CH6_SW!=prev_CH6)
			{ // LED switch is temporary
				len=43;
				prev_CH6=CH6_SW;
			}
			if(len)
			{ // Send the light flag for a couple of packets
				packet[4] |= GD00X_V2_FLAG_LIGHT;
				len--;
			}

			packet[3]=(packet[0]+packet[1]+packet[2]+packet[4])^(crc8);

			if( (packet_count%12) == 0 )
				hopping_frequency_no ^= 1;			// Toggle between the 2 frequencies
			packet_count++;
			if(packet_count>34) packet_count=0;		// Full period
			if( seq == (((packet_count*3)/7)%5) )
			{
				if(packet_period==2700)
					packet_period=3000;
				else
					packet_period=2700;
			}
			else
				packet_period=4300;
		}
		packet[5]='D';
	}

	// Power on, TX mode, CRC enabled
	XN297_Configure(_BV(NRF24L01_00_EN_CRC) | _BV(NRF24L01_00_CRCO) | _BV(NRF24L01_00_PWR_UP));
	if(IS_BIND_DONE)
	{
		NRF24L01_WriteReg(NRF24L01_05_RF_CH, hopping_frequency[hopping_frequency_no]);
		if(sub_protocol==GD_V1)
		{
			hopping_frequency_no++;
			hopping_frequency_no &= 3;			// 4 RF channels
		}
	}

	NRF24L01_WriteReg(NRF24L01_07_STATUS, 0x70);
	NRF24L01_FlushTx();
	XN297_WritePayload(packet, packet_length);

	NRF24L01_SetPower();	// Set tx_power
}

static void __attribute__((unused)) GD00X_init()
{
	NRF24L01_Initialize();
	NRF24L01_SetTxRxMode(TX_EN);
	if(sub_protocol==GD_V1)
		XN297_SetTXAddr((uint8_t*)"\xcc\xcc\xcc\xcc\xcc", 5);
	else
		XN297_SetTXAddr((uint8_t*)"GDKNx", 5);

	NRF24L01_WriteReg(NRF24L01_05_RF_CH, sub_protocol==GD_V1?GD00X_RF_BIND_CHANNEL:GD00X_V2_RF_BIND_CHANNEL);	// Bind channel
	NRF24L01_FlushTx();
	NRF24L01_FlushRx();
	NRF24L01_WriteReg(NRF24L01_07_STATUS, 0x70);	// Clear data ready, data sent, and retransmit
	NRF24L01_WriteReg(NRF24L01_01_EN_AA, 0x00);		// No Auto Acknowldgement on all data pipes
	NRF24L01_WriteReg(NRF24L01_02_EN_RXADDR, 0x01);	// Enable data pipe 0 only
	NRF24L01_SetBitrate(NRF24L01_BR_250K);			// 250Kbps
	NRF24L01_SetPower();
}

static void __attribute__((unused)) GD00X_initialize_txid()
{
	if(sub_protocol==GD_V1)
	{
		uint8_t start=76+(rx_tx_addr[0]&0x03);
		for(uint8_t i=0; i<4;i++)
			hopping_frequency[i]=start-(i<<1);
		#ifdef FORCE_GD00X_ORIGINAL_ID
			rx_tx_addr[0]=0x1F;					// or 0xA5 or 0x26
			rx_tx_addr[1]=0x39;					// or 0x37 or 0x35
			rx_tx_addr[2]=0x12;					// Constant on 3 TXs
			rx_tx_addr[3]=0x13;					// Constant on 3 TXs
			for(uint8_t i=0; i<4;i++)
				hopping_frequency[i]=79-(i<<1);	// or 77 or 78
		#endif
	}
	else
	{
		//Generate 64 different IDs
		rx_tx_addr[1]=0x00;
		rx_tx_addr[2]=0x00;
		rx_tx_addr[1+((rx_tx_addr[3]&0x10)>>4)]=rx_tx_addr[3]&0x8F;
		rx_tx_addr[0]=0x65;
		rx_tx_addr[3]=0x95;
		rx_tx_addr[4]=0x47;	//'G'

		crc8=rx_tx_addr[0]^rx_tx_addr[1]^rx_tx_addr[2];
		//hopping calculation
		hopping_frequency[0]=(0x15+(crc8^rx_tx_addr[3]))&0x1F;
		if( hopping_frequency[0] == 0x0F )
			hopping_frequency[0]=0x0E;
		else if( (hopping_frequency[0]&0xFE) == 0x10 )
			hopping_frequency[0]+=2;
		hopping_frequency[1]=0x20+hopping_frequency[0];

		#ifdef FORCE_GD00X_ORIGINAL_ID
			//ID 1
			rx_tx_addr[0]=0x65;
			rx_tx_addr[1]=0x00;
			rx_tx_addr[2]=0x00;
			rx_tx_addr[3]=0x95;
			rx_tx_addr[4]=0x47;	//'G'
			hopping_frequency[0]=0x05;
			hopping_frequency[1]=0x25;
			//ID 2
			rx_tx_addr[0]=0xFD;
			rx_tx_addr[1]=0x09;
			rx_tx_addr[2]=0x00;
			rx_tx_addr[3]=0x65;
			rx_tx_addr[4]=0x47;	//'G'
			hopping_frequency[0]=0x06;
			hopping_frequency[1]=0x26;
			//ID 3
			rx_tx_addr[0]=0x67;
			rx_tx_addr[1]=0x0F;
			rx_tx_addr[2]=0x00;
			rx_tx_addr[3]=0x69;
			rx_tx_addr[4]=0x47;	//'G'
			hopping_frequency[0]=0x16;
			hopping_frequency[1]=0x36;
		#endif
	}
}

uint16_t GD00X_callback()
{
	if(IS_BIND_IN_PROGRESS)
		if(--bind_counter==0)
			BIND_DONE;
	GD00X_send_packet();
	return packet_period;
}

uint16_t initGD00X()
{
	BIND_IN_PROGRESS;	// autobind protocol
	GD00X_initialize_txid();
	GD00X_init();
	hopping_frequency_no = 0;
	bind_counter=GD00X_BIND_COUNT;
	packet_period=sub_protocol==GD_V1?GD00X_PACKET_PERIOD:GD00X_V2_BIND_PACKET_PERIOD;
	packet_length=sub_protocol==GD_V1?GD00X_PAYLOAD_SIZE:GD00X_V2_PAYLOAD_SIZE;
	packet_count=0;
	len=0;
	return GD00X_INITIAL_WAIT;
}

#endif
