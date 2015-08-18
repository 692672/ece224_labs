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

//Size of sector buffers and sample rate
#define BUF_SIZE 512
#define SAMPLE_RATE 44100

//Numerical values for switches
#define SW_NORMALPLAY 0
#define SW_DOUBLEPLAY 1
#define SW_HALFPLAY 2
#define SW_DELAYPLAY 3
#define SW_REVERSEPLAY 4

//Numerical values for buttons
#define BTN_STOP 1
#define BTN_PLAY 2
#define BTN_NEXT 4
#define BTN_PREV 8

volatile static int _playing = 0;
volatile static int _isredge = 0;

volatile static data_file _fileinfo;
volatile static alt_u8 _swstate = 0x0;

UINT16 BytePrep(int i, int j){
	//left shift j byte and or it with i, to prepare to write to audio
	return (UINT16)((j << 8) | i);
}

//Play song in regular speed
int NormalPlay(data_file file_, int l_, int* clusters_)
{
	int i, j;
	BYTE playBuffer[BUF_SIZE] = {0};

	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		//If play flag is defaulted (user stops music), then stop playing from buffer
		if (_playing == 0)
			break;

		//Buffer current sector (i) from cluster into the playBuffer
		get_rel_sector(&file_, playBuffer, clusters_, i);
		for (j = 0; j < BUF_SIZE; j+=2){

			//Wait for signal to write for audio, then write to it with prepared bytes
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 , BytePrep(playBuffer[j], playBuffer[j + 1]));
		}
	}
}

//Skips every other sample to make it seem like its playing double speed.
int DoublePlay(data_file file_, int l_, int* clusters_)
{
	int i, j = 0;
	BYTE playBuffer[BUF_SIZE] = {0};

	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		if (_playing == 0)
			break;

		//Buffer current sector (i) from cluster into the playBuffer
		get_rel_sector(&file_, playBuffer, clusters_, i);
		for (j = 0; j < BUF_SIZE; j+=2)
		{
			//Wait for signal to write for audio, then write to it with prepared bytes
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 , BytePrep(playBuffer[j], playBuffer[j + 1]));

			//Skip 2 lefts and 2 rights every 2 lefts and 2 rights... get it? HAHAHAHAA
			if (j % 4 == 0)
				j += 4;
		}
	}
}

//Duplicate every other sample to make it seem like its half speed
int HalfPlay(data_file file_, int l_, int* clusters_)
{
	int i, j, r = 0;
	BYTE playBuffer[BUF_SIZE] = {0};

	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		if (_playing == 0)
			break;

		//Buffer current sector (i) from cluster into the playBuffer
		get_rel_sector(&file_, playBuffer, clusters_, i);
		for (j = 0; j < BUF_SIZE; j+=2)
		{
			//Wait for signal to write for audio, then write to it with prepared bytes
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 , BytePrep(playBuffer[j], playBuffer[j + 1]));

			//Logic to play the bytes twice (repeat every 4 bits)
			if (j % 4 == 0)
			{
				if (r == 1){
					r = 0;
				}else{
					j -= 4;
					r = 1;
				}
			}
		}
	}
}

//Plays the audio backwards by iterating from end of buffer to the beginning
int ReversePlay(data_file file_, int l_, int* clusters_)
{
	int i, j;
	BYTE playBuffer[BUF_SIZE] = {0};

	for (i = l_ * BPB_SecPerClus; i > 0; i--)
	{
		if (_playing == 0)
			break;

		get_rel_sector(&file_, playBuffer, clusters_, i);

		//Starts from the end and works backwards
		for (j = 508; j > 0; j-=6)
		{
			//Wait for signal to write for audio, then write to it with prepared bytes
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 , BytePrep(playBuffer[j], playBuffer[j + 1]));

			//To play the next 2 bits of reverse batch (right side)
			j+=2;
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 , BytePrep(playBuffer[j], playBuffer[j + 1]));
		}
	}
}


int DelayPlay(data_file file_, int l_, int* clusters_)
{
	int i, j;

	//Create an additional delay buffer with the size of sample rate
	BYTE playBuffer[BUF_SIZE] = {0};
	UINT16 delayBuffer[SAMPLE_RATE] = {0};

	int idxDelay, flag = 0;

	//Iterate through the audio
	for (i = 0; i < l_ * BPB_SecPerClus; i++)
	{
		if (_playing == 0)
			break;

		get_rel_sector(&file_, playBuffer, clusters_, i);

		for (j = 0; j < BUF_SIZE; j+=2)
		{
			//Wait for signal to write for audio, then write to it with prepared bytes
			while(IORD(AUD_FULL_BASE, 0)){}

			//Populate delayBuffer using idxDelay with what's in playBuffer on an edge
			if (flag == 0){
				IOWR(AUDIO_0_BASE, 0 , BytePrep(playBuffer[j], playBuffer[j + 1]));
				flag = 1;
			}else{
				IOWR(AUDIO_0_BASE, 0, delayBuffer[idxDelay]);
				delayBuffer[idxDelay] = BytePrep(playBuffer[j], playBuffer[j + 1]);
				flag = 0;
			}

			//If we exceed the buffer size, loop around to beginning of array
			idxDelay++;
			if (idxDelay > SAMPLE_RATE)
				idxDelay = idxDelay % SAMPLE_RATE;
		}
	}

	//Finish from current delay index to end of delay array
	for (i = idxDelay; i < SAMPLE_RATE; i++)
	{
		if (_playing == 0)
			break;

		while(IORD(AUD_FULL_BASE, 0)){}
		IOWR(AUDIO_0_BASE, 0, delayBuffer[i]);
	}

	//Finish last remaining audio data from beginning to delayIdx
	for (i = 0; i < idxDelay; i++)
	{
		if (_playing == 0)
			break;

		while(IORD(AUD_FULL_BASE, 0)){}
		IOWR(AUDIO_0_BASE, 0, delayBuffer[i]);
	}
}

//Get the current switch value and display the paly mode & song to LCD
static void DisplayStatusLCD()
{
	_swstate = _swstate & 0x07;
	LCD_Display(_fileinfo.Name, _swstate);
}

//This is used to set up the buttons to do their required roles.
static void button_ISR(void* context, alt_u32 id)
{
	//Listen to ISR on a specific edge, so that we don't receive 2 interrupts at once
	if(_isredge == 0){
		alt_u8 btnPressed = IORD(BUTTON_PIO_BASE, 3) & 0xf;

		switch(btnPressed){
			//Set playing flag to default value and update the status to LCD
			case BTN_STOP:
				_playing = 0;
				IOWR(BUTTON_PIO_BASE, 2, 0xf);
				DisplayStatusLCD();
			break;

			//Set play flag to high and let while loop handle audio play (disable other buttons)
			case BTN_PLAY:
				_playing = 1;
				IOWR(BUTTON_PIO_BASE, 2, 0xf);
			break;
			case BTN_NEXT:
				if(search_for_filetype("WAV", &_fileinfo, 0, 1) == 0)
					DisplayStatusLCD();
			break;
			case BTN_PREV:
				//Prevent file iterator from getting stuck on the first file when going previous
				if(file_number < 0)
					file_number = 0;
				else
					file_number = file_number - 2;

				if(search_for_filetype("WAV", &_fileinfo, 0, 1) == 0)
					DisplayStatusLCD();
			break;
		}
		_isredge = 1;
	}else{
		_isredge = 0;
	}

	IOWR(BUTTON_PIO_BASE, 3, 0x0);
}

int main()
{
	alt_u8 _prevswstate = 0x0;

	Setup();

	//Set up the initial file on SD card
	search_for_filetype("WAV", &_fileinfo, 0, 1);
	_swstate = IORD(SWITCH_PIO_BASE, 0);
	_prevswstate = 8;

	while(1)
	{
		//Listen for any changes in the switches, update if so
		_swstate = IORD(SWITCH_PIO_BASE, 0);
		if(_swstate != _prevswstate){
			DisplayStatusLCD();
			_prevswstate = _swstate & 0x07;
		}

		if (_playing == 1)
		{
			DisplayStatusLCD();
			printf("Playing audio file: %s\n", _fileinfo.Name);

			//Disable other buttons
			IOWR(BUTTON_PIO_BASE, 2, 0x01);

			//Initialise cluster chain and build it from the audio file
			int clusterChain[100000];
			int tracklength = 1 + ceil(_fileinfo.FileSize/(BPB_BytsPerSec*BPB_SecPerClus));

			LCD_File_Buffering(_fileinfo.Name);
			build_cluster_chain(clusterChain, tracklength, &_fileinfo);

			//Update LCD and play audio dependent on user-selected mode
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

			//Enable buttons, and reset play flag
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

	IOWR(BUTTON_PIO_BASE, 2, 0xf);
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
	alt_irq_register(BUTTON_PIO_IRQ, (void*)0, button_ISR);
}
