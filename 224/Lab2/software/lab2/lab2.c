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
#define SAMPLE_RATE 44100

#define SW_NORMALPLAY 0
#define SW_DOUBLEPLAY 1
#define SW_HALFPLAY 2
#define SW_DELAYPLAY 3
#define SW_REVERSEPLAY 4

#define BTN_STOP 1
#define BTN_PLAY 2
#define BTN_NEXT 4
#define BTN_PREV 8

volatile static int _playing = 0;
volatile static int _edge = 0;

volatile static data_file _fileinfo;
volatile static alt_u8 _swstate = 0x0;

//Play song in regular speed
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
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)(playBuffer[j + 1] << 8) | (playBuffer[j]));
		}
	}
}

//Truncates every other bit to make it seem like its playing half speed.
int DoublePlay(data_file file_, int l_, int* clusters_)
{
	int i, j = 0;
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

//Duplicate every bit to make it seem like its half speed
int HalfPlay(data_file file_, int l_, int* clusters_)
{
	int i, j, r = 0;
	BYTE playBuffer[BUF_SIZE] = {0};

	int skip = 0;
	int comp = 0;

	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		if (_playing == 0)
			i = l_ * BPB_SecPerClus;

		get_rel_sector(&file_, playBuffer, clusters_, i);
		for (j = 0; j < BUF_SIZE; j+=2)
		{
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)(playBuffer[j + 1] << 8) | (playBuffer[j]));
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

		//Starts from the end and works backwards
		for (j = 508; j > 0; j-=6)
		{
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)(playBuffer[j + 1] << 8) | (playBuffer[j]));
			j+=2;

			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)(playBuffer[j + 1] << 8) | (playBuffer[j]));
		}
	}
}


int DelayPlay(data_file file_, int l_, int* clusters_)
{
	int i, j;
	BYTE playBuffer[BUF_SIZE] = {0};
	UINT16 delayBuffer[SAMPLE_RATE] = {0};

	int idxDelay, flag = 0;

	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		if (_playing == 0)
			i = l_ * BPB_SecPerClus;

		get_rel_sector(&file_, playBuffer, clusters_, i);

		for (j = 0; j < BUF_SIZE; j+=2)
		{
			while(IORD(AUD_FULL_BASE, 0)){}

			if (flag == 0){
				IOWR(AUDIO_0_BASE, 0 ,(UINT16)(playBuffer[j + 1] << 8) | (playBuffer[j]));
				flag = 1;
			}else{
				IOWR(AUDIO_0_BASE, 0, delayBuffer[idxDelay]);
				delayBuffer[idxDelay] = (UINT16)(playBuffer[j + 1] << 8) | (playBuffer[j]);
				flag = 0;
			}

			idxDelay++;
			if (idxDelay > SAMPLE_RATE)
				idxDelay = idxDelay % SAMPLE_RATE;
		}
	}

	for (i = idxDelay; i < SAMPLE_RATE; i++)
	{
		if (_playing == 0)
			i = SAMPLE_RATE;

		while(IORD(AUD_FULL_BASE, 0)){}
		IOWR(AUDIO_0_BASE, 0, delayBuffer[i]);
	}

	for (i = 0; i < idxDelay; i++)
	{
		if (_playing == 0)
			i = idxDelay;

		while(IORD(AUD_FULL_BASE, 0)){}
		IOWR(AUDIO_0_BASE, 0, delayBuffer[i]);
	}
}

static void DisplayStatusLCD()
{
	_swstate = _swstate & 0x07;
	LCD_Display(_fileinfo.Name, _swstate);
}

//This is used to set up the buttons to do their required roles.
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
	alt_u8 _prevswstate = 0x0;

	Setup();

	IOWR(BUTTON_PIO_BASE, 2, 0xf);
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
	alt_irq_register(BUTTON_PIO_IRQ, (void*)0, button_ISR);

	BYTE localBuffer[BUF_SIZE] = {0};
	SD_read_lba(localBuffer,0,1);

	search_for_filetype("WAV", &_fileinfo, 0, 1);
	_swstate = IORD(SWITCH_PIO_BASE, 0);
	_prevswstate = 8;

	while(1)
	{
		_swstate = IORD(SWITCH_PIO_BASE, 0);
		if(_swstate != _prevswstate){
			DisplayStatusLCD();
			_prevswstate = _swstate & 0x07;
		}

		if (_playing == 1)
		{
			DisplayStatusLCD();

			printf("Playing audio file: %s\n", _fileinfo.Name);
			IOWR(BUTTON_PIO_BASE, 2, 0x01);

			int clusterChain[100000];
			int tracklength = 1 + ceil(_fileinfo.FileSize/(BPB_BytsPerSec*BPB_SecPerClus));

			LCD_File_Buffering(_fileinfo.Name);
			build_cluster_chain(clusterChain, tracklength, &_fileinfo);

			DisplayStatusLCD();
			switch(_swstate){
				case SW_NORMALPLAY:
					NormalPlay(_fileinfo, tracklength, clusterChain);
					break;
				case SW_DOUBLEPLAY:
					DoublePlay(_fileinfo, tracklength, clusterChain);
					break;
				case SW_HALFPLAY:
					HalfPlay(_fileinfo, tracklength, clusterChain);
					break;
				case SW_DELAYPLAY:
					DelayPlay(_fileinfo, tracklength, clusterChain);
					break;
				case SW_REVERSEPLAY:
					ReversePlay(_fileinfo, tracklength, clusterChain);
					break;
				default:
					NormalPlay(_fileinfo, tracklength, clusterChain);
					break;
			}

			IOWR(BUTTON_PIO_BASE, 2, 0xf);
			_playing = 0;
		}
	}

	return 0;
}

//Setups up required aspects for the program to run.
void Setup(){
	SD_card_init();
	init_mbr();
	init_bs();
	init_audio_codec();
}
