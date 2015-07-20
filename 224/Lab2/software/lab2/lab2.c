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
#define SW_DELAYPLAY 3
#define SW_REVERSEPLAY 4

#define BTN_STOP 1
#define BTN_PLAY 2
#define BTN_NEXT 4
#define BTN_PREV 8


volatile static alt_u8 switch_state = 0x0;
volatile static data_file df;

volatile static int isPlaying = 0;
volatile static int currentEdge = 0;

int NormalPlay(data_file df, int length, int* cc)
{
	int i, j;
	BYTE buffer[BUF_SIZE] = {0};

	for (i = 0; i < length * BPB_SecPerClus; i++)
	{
		if (isPlaying == 0){
			i = length * BPB_SecPerClus;
		}
		get_rel_sector(&df, buffer, cc, i);

		for (j = 0; j < BUF_SIZE; j+=2){
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)(buffer[j + 1] << 8 ) | ( buffer[j]));
		}
	}
}

int DoublePlay(data_file df, int length, int* cc)
{
	int i, j, skip = 0;
	BYTE buffer[BUF_SIZE] = {0};

	for (i = 0; i < length * BPB_SecPerClus; i++)
	{
		if (isPlaying == 0){
			i = length * BPB_SecPerClus;
		}
		get_rel_sector(&df, buffer, cc, i);

		for (j = 0; j < BUF_SIZE; j+=2)
		{
			skip++;

			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)(buffer[j + 1] << 8) | (buffer[j]));

			if (skip == 2)
			{
				j += 4;
				skip = 0;
			}
		}
	}
}

int HalfPlay(data_file df, int length, int* cc)
{
	int i, j, repeat = 0;
	BYTE buffer[BUF_SIZE] = {0};
	for (i = 0; i < length * BPB_SecPerClus; i++)
	{
		if (isPlaying == 0){
			i = length * BPB_SecPerClus;
		}
		get_rel_sector(&df, buffer, cc, i);

		for (j = 0; j < BUF_SIZE; j+=2)
		{
			while(IORD(AUD_FULL_BASE, 0)){}
			IOWR(AUDIO_0_BASE, 0 ,(UINT16)( buffer[j + 1] << 8 ) | ( buffer[j] ));
			if (((j + 2) % 4) == 0)
			{
				if (repeat == 0){
					j -= 4;
					repeat = 1;
				}else{
					repeat = 0;
				}
			}
		}
	}
}

int ReversePlay(data_file df, int length, int* cc)
{
	int i, j;
	BYTE buffer[BUF_SIZE] = {0};

	for (i = length * BPB_SecPerClus; i > 0; i--)
	{
		if (isPlaying == 0){
			i = -1;
		}
		get_rel_sector(&df, buffer, cc, i);

		for (j = 508; j > 0; j-=6)
		{
			while(IORD(AUD_FULL_BASE, 0)){}

			IOWR(AUDIO_0_BASE, 0 ,(UINT16)( buffer[j + 1] << 8 ) | ( buffer[j] ));
			j+=2;

			while(IORD(AUD_FULL_BASE, 0)){}

			IOWR(AUDIO_0_BASE, 0 ,(UINT16)( buffer[j + 1] << 8 ) | ( buffer[j] ));
		}
	}
}

int DelayPlay(data_file df, int length, int* cc)
{
	int i, j, k;
	BYTE buffer[512] = {0};
	UINT16 delay_buff[88200] = {0};
	int delay_index = 0;
	int toggle = 0;
	int start_play = 0;

	for (i = 0; i < length * BPB_SecPerClus; i++)
	{
		if (isPlaying == 0){
			i = length * BPB_SecPerClus;
		}
		get_rel_sector(&df, buffer, cc, i);

		for (j = 0; j < 512; j+=2)
		{
			if (toggle == 0){
				while(IORD(AUD_FULL_BASE, 0)){}

				IOWR(AUDIO_0_BASE, 0 ,(UINT16)( buffer[j + 1] << 8 ) | ( buffer[j] ));
				toggle = 1;
			}else{
				while(IORD(AUD_FULL_BASE, 0)){}

				IOWR(AUDIO_0_BASE, 0, delay_buff[delay_index]);
				delay_buff[delay_index] = (UINT16)( buffer[j + 1] << 8 ) | ( buffer[j] );
				toggle = 0;
			}

			delay_index++;
			if (delay_index > 88200)
				delay_index = 0;
		}
	}

	// Play the last second remaining in the right buffer
	for (k = 0; k < 176400; k++)
	{
		if (isPlaying == 0){
			k = 176400;
		}

		if (toggle == 1){
			while(IORD(AUD_FULL_BASE, 0)){}

			IOWR(AUDIO_0_BASE, 0, delay_buff[delay_index]);
			toggle = 0;
		}else{
			while(IORD(AUD_FULL_BASE, 0)){}

			IOWR(AUDIO_0_BASE, 0, 0);
			toggle = 1;
		}

		delay_index++;
		if (delay_index > 88200)
			delay_index = 0;
	}
}

// Displays the current playback mode on the LCD
static void display_LCD_mode()
{
	switch_state = switch_state & 0x07;
	LCD_Display(df.Name, switch_state);
}

static void PlayAudio()
{
	int cChain[100000];
	int length = 1 + ceil(df.FileSize/(BPB_BytsPerSec*BPB_SecPerClus));

	//Let user know that audio is buffering
	LCD_File_Buffering(df.Name);
	build_cluster_chain(cChain, length, &df);
	LCD_File_Playing(df.Name);

	switch(switch_state){
		case SW_NORMALPLAY:
			NormalPlay(df, length, cChain);
			break;
		case SW_DOUBLEPLAY:
			DoublePlay(df, length, cChain);
			break;
		case SW_HALFPLAY:
			HalfPlay(df, length, cChain);
			break;
		case SW_DELAYPLAY:
			DelayPlay(df, length, cChain);
			break;
		case SW_REVERSEPLAY:
			ReversePlay(df, length, cChain);
			break;
	}
}

static void button_ISR(void* context, alt_u32 id)
{
	if (currentEdge == 0)
	{
		currentEdge = 1;
		alt_u8 btnPressed = IORD(BUTTON_PIO_BASE, 3) & 0xf;

		switch(btnPressed){
			case BTN_STOP:
				isPlaying = 0;
				IOWR(BUTTON_PIO_BASE, 2, 0xf);
			break;
			case BTN_PLAY:
				isPlaying = 1;
				IOWR(BUTTON_PIO_BASE, 2, 0xf);
			break;
			case BTN_NEXT:
				search_for_filetype("WAV", &df, 0, 1);
			break;
			case BTN_PREV:
				if(file_number > 0)
					file_number = file_number - 2;
				
				search_for_filetype("WAV", &df, 0, 1);
			break;
		}

		display_LCD_mode();
	}
	else
	{
		currentEdge = 0;
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
	search_for_filetype("WAV", &df, 0, 1);
	switch_state = IORD(SWITCH_PIO_BASE, 0);
	display_LCD_mode();

	while(1)
	{
		switch_state = IORD(SWITCH_PIO_BASE, 0);
		if (isPlaying == 1)
		{
			display_LCD_mode();
			printf("Playing audio file: %s\n", df.Name);

			// Disable all buttons except the stop button.
			IOWR(BUTTON_PIO_BASE, 2, 0x01);
			PlayAudio();

			// Enable all the buttons.
			IOWR(BUTTON_PIO_BASE, 2, 0xf);
			isPlaying = 0;
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

void LCD_File_Playing(char* Text)
{
  char parsed_text[12];
  char Text2[16] = {"Playing"};
  int i;

  for(i=0;i<11;i++)
  {
    parsed_text[i] = Text[i];
  }

  parsed_text[11] = '\0';

  LCD_Init();
  LCD_Show_Text(parsed_text);
  LCD_Line2();
  LCD_Show_Text(Text2);
}
