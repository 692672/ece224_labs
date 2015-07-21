#include "alt_types.h"
#include <stdio.h>
#include "system.h"
#include "sys/alt_irq.h"
#include <io.h>
#include <math.h>

#include "SD_Card.h"
#include "fat.h"
#include "LCD.h"
#include "wm8731.h"
#include "basic_io.h"

#define BUF_SIZE 512

#define SW_NORMALPLAY 0
#define SW_DOUBLEPLAY 1
#define SW_HALFPLAY 2
#define SW_REVERSEPLAY 3
#define SW_DELAYPLAY 4

#define BTN_STOP 1
#define BTN_PLAY 2
#define BTN_NEXT 4
#define BTN_PREV 8

volatile static alt_u8 switch_state = 0x0;
volatile static data_file _fileinfo;

volatile static int _playing = 0;
volatile static int _edge = 0;

/* Various modes to play the audio file */
int NormalPlay(data_file file_, int l_, int* clusters_)
{
	int i, j;
	BYTE playBuffer[BUF_SIZE] = {0};

	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		if (_playing == 0)
			i = l_ * BPB_SecPerClus;

		get_rel_sector(&file_, playBuffer, clusters_, i);
		for (j = 0; j < BUF_SIZE; j+=2){
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)(playBuffer[j + 1] << 8 ) | ( playBuffer[j]));
		}
	}
}

int DoublePlay(data_file file_, int l_, int* clusters_)
{
	int i, j, skip = 0;
	BYTE playBuffer[BUF_SIZE] = {0};

	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		if (_playing == 0)
			i = l_ * BPB_SecPerClus;

		get_rel_sector(&file_, playBuffer, clusters_, i);
		for (j = 0; j < BUF_SIZE; j+=2)
		{
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)(playBuffer[j + 1] << 8) | (playBuffer[j]));

			if (j % 4 == 0)
				j += 4;
		}
	}
}

int HalfPlay(data_file file_, int l_, int* clusters_)
{
	int i, j, r = 0;
	BYTE playBuffer[BUF_SIZE] = {0};
	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		if (_playing == 0)
			i = l_ * BPB_SecPerClus;

		get_rel_sector(&file_, playBuffer, clusters_, i);
		for (j = 0; j < BUF_SIZE; j+=2)
		{
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)( playBuffer[j + 1] << 8 ) | ( playBuffer[j] ));
			if (((j + 2) % 4) == 0)
			{
				if (r == 0){
					j -= 4;
					r = 1;
				}else{
					r = 0;
				}
			}
		}
	}
}

int ReversePlay(data_file file_, int l_, int* clusters_)
{
	int i, j;
	BYTE playBuffer[BUF_SIZE] = {0};

	for (i = l_ * BPB_SecPerClus; i > 0; i--)
	{
		if (_playing == 0)
			i = -1;

		get_rel_sector(&file_, playBuffer, clusters_, i);

		for (j = 508; j > 0; j-=6)
		{
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)( playBuffer[j + 1] << 8 ) | ( playBuffer[j] ));
			j+=2;

			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)( playBuffer[j + 1] << 8 ) | ( playBuffer[j] ));
		}
	}
}


int DelayPlay(data_file file_, int l_, int* clusters_)
{
	int i, j, k;
	BYTE playBuffer[512] = {0};
	UINT16 delayBuffer[88200] = {0};

	int idxDelay, flag = 0;

	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		if (_playing == 0)
			i = l_ * BPB_SecPerClus;

		get_rel_sector(&file_, playBuffer, clusters_, i);

		for (j = 0; j < 512; j+=2)
		{
			if (flag == 0){
				while(IORD(AUD_FULL_BASE, 0)){}
				IOWR(AUDIO_0_BASE, 0 ,(UINT16)( playBuffer[j + 1] << 8 ) | ( playBuffer[j] ));
				flag = 1;
			}else{
				while(IORD(AUD_FULL_BASE, 0)){}
				IOWR(AUDIO_0_BASE, 0, delayBuffer[idxDelay]);
				delayBuffer[idxDelay] = (UINT16)( playBuffer[j + 1] << 8 ) | ( playBuffer[j] );
				flag = 0;
			}

			idxDelay++;
			if (idxDelay > 88200)
				idxDelay = 0;
		}
	}

	// Play the last second remaining in the right buffer
	for (k = 0; k < 88200; k++)
	{
		if (_playing == 0)
			k = 88200;

		if (flag == 1){
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0, delayBuffer[idxDelay]);
			flag = 0;
		}else{
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0, 0);
			flag = 1;
		}

		idxDelay++;
		if (idxDelay > 88200)
			idxDelay = 0;
	}
}

// Displays the current playback mode on the LCD
static void DisplayStatusLCD()
{
	switch_state = switch_state & 0x07;
	LCD_Display(_fileinfo.Name, switch_state);
}

static void button_ISR(void* context, alt_u32 id)
{
	if (_edge == 0)
	{
		_edge = 1;
		alt_u8 btnPressed = IORD(BUTTON_PIO_BASE, 3) & 0xf;

		switch(btnPressed){
			case BTN_STOP:
				_playing = 0;

				IOWR(BUTTON_PIO_BASE, 2, 0xf);
				DisplayStatusLCD();
			break;
			case BTN_PLAY:
				_playing = 1;

				IOWR(BUTTON_PIO_BASE, 2, 0xf);
			break;
			case BTN_NEXT:
				if(search_for_filetype("WAV", &_fileinfo, 0, 1) == 0)
					DisplayStatusLCD();
			break;
			case BTN_PREV:
				if(file_number < 0)
					file_number = 0;
				else
					file_number = file_number - 2;

				if(search_for_filetype("WAV", &_fileinfo, 0, 1) == 0)
					DisplayStatusLCD();
			break;
		}
	}
	else
	{
		_edge = 0;
	}

	// Clear Interrupt
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
}

int main()
{
	Setup();

	IOWR(BUTTON_PIO_BASE, 2, 0xf);
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
	alt_irq_register(BUTTON_PIO_IRQ, (void*)0, button_ISR);

	BYTE buff[BUF_SIZE] = {0};
	SD_read_lba(buff,0,1);

	//Search for the data file, set up initial switches and display on LCD
	search_for_filetype("WAV", &_fileinfo, 0, 1);
	switch_state = IORD(SWITCH_PIO_BASE, 0);
	DisplayStatusLCD();

	while(1)
	{
		switch_state = IORD(SWITCH_PIO_BASE, 0);
		if (_playing == 1)
		{
			DisplayStatusLCD();
			printf("Playing audio file: %s\n", _fileinfo.Name);

			// Disable all buttons except the stop button.
			IOWR(BUTTON_PIO_BASE, 2, 0x01);
			int clusterChain[100000];
			int length = 1 + ceil(_fileinfo.FileSize/(BPB_BytsPerSec*BPB_SecPerClus));

			//Let user know that audio is buffering
			LCD_File_Buffering(_fileinfo.Name);
			build_cluster_chain(clusterChain, length, &_fileinfo);

			DisplayStatusLCD();
			switch(switch_state){
				case SW_NORMALPLAY:
					NormalPlay(_fileinfo, length, clusterChain);
					break;
				case SW_DOUBLEPLAY:
					DoublePlay(_fileinfo, length, clusterChain);
					break;
				case SW_HALFPLAY:
					HalfPlay(_fileinfo, length, clusterChain);
					break;
				case SW_DELAYPLAY:
					DelayPlay(_fileinfo, length, clusterChain);
					break;
				case SW_REVERSEPLAY:
					ReversePlay(_fileinfo, length, clusterChain);
					break;
				default:
					NormalPlay(_fileinfo, length, clusterChain);
					break;
			}

			IOWR(BUTTON_PIO_BASE, 2, 0xf);
			_playing = 0;
		}
	}

	return 0;
}

void Setup(){
	SD_card_init();
	init_mbr();
	init_bs();
	init_audio_codec();
}
