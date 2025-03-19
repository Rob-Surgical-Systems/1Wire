//---------------------------------------------------------------------------
// Copyright (C) 2000 Dallas Semiconductor Corporation, All Rights Reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY,  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL DALLAS SEMICONDUCTOR BE LIABLE FOR ANY CLAIM, DAMAGES
// OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// Except as contained in this notice, the name of Dallas Semiconductor
// shall not be used except as stated in the Dallas Semiconductor
// Branding Policy.
//---------------------------------------------------------------------------
//
//  ds390lnk.C - Serial functions using 8051 to be used as a test
//               for DS2480 based Universal Serial Adapter 'U'
//               functions.
//
//  Version: 1.00
//
//  TODO:	1) Clean up ugly code.
//			2) Allow for compile-time choice of using Timer2 for baud rate of Serial1
//			3) Package Serial0 and Serial1 SFRs into nice structs, hopefully to allow
//			   for removal of all the if-else statements in each function.
//			4) Do the math and write a better msDelay function.
//

#include "microser.h"

#if USE_SERIAL_INTERRUPTS
	// this is a ring buffer and can overflow at anytime!
	static volatile unsigned char receiveBuffer0[SERIAL_RECEIVE_BUFFER_SIZE];
	static volatile int receiveBufferHead0 = 0;
	static volatile int receiveBufferTail0 = 0;
	static volatile unsigned char receiveBuffer1[SERIAL_RECEIVE_BUFFER_SIZE];
	static volatile int receiveBufferHead1 = 0;
	static volatile int receiveBufferTail1 = 0;

	// no buffering for transmit
	static volatile char transmitIsBusy0 = 0;
	static volatile char transmitIsBusy1 = 0;

#endif


//---------------------------------------------------------------------------
//-------- COM required functions for MLANU
//---------------------------------------------------------------------------
/* exportable functions */
SMALLINT  OpenCOM(int, char*);
SMALLINT  WriteCOM(int, int, uchar*);
void      CloseCOM(int);
void      FlushCOM(int);
int       ReadCOM(int, int, uchar*);
void      BreakCOM(int);
void      SetBaudCOM(int, uchar);
void      msDelay(int);
long      msGettick(void);

//---------------------------------------------------------------------------
// Attempt to open a com port.  Keep the handle in ComID.
// Set the starting baud rate to 9600.
//
// 'port_zstr' - zero terminate port name.
//
// Returns: valid handle, or -1 if an error occurred
//
int OpenCOMEx(char *port_zstr)
{
   int portnum;

   if(port_zstr[0] == '0')
   {
      portnum = 0;
   }
   else
   {
      portnum = 1;
   }

   if(!OpenCOM(portnum, NULL))
   {
      return -1;
   }

   return portnum;
}

//---------------------------------------------------------------------------
// Attempt to open a com port.  Keep the handle in ComID.
// Set the starting baud rate to 9600.
//
// 'portnum'   - number 0 to MAX_PORTNUM-1.  This number provided will
//               be used to indicate the port number desired when calling
//               all other functions in this library.
//
// 'port_zstr' - zero terminate port name.  NOT USED.
//
//
// Returns: TRUE(1)  - success, COM port opened
//          FALSE(0) - failure, could not open specified port
//
SMALLINT OpenCOM(int portnum, char *port_zstr)
{
	port_zstr = 0; //to silence compiler

   EA = 0; //Disable interrupts

#if USE_TIMER2_FOR_SERIAL0
   if(portnum==0)
   {
   	TH2 = RCAP2H = 0xFF;
   	T2CON |= 0x30;	//enable Timer2 control for serial1
   	T2MOD = (T2MOD&0x0f) | 0x20; // timer 2 is an 8bit auto-reload counter
   }
   else
   {
#else
   	T2CON &= 0xCF;	//disable Timer2 control for serial0
#endif
      //Setup Timer1 for baud generation
   	TMOD = (TMOD&0x0f) | 0x20; // timer 1 is an 8bit auto-reload counter
#if USE_TIMER2_FOR_SERIAL0
   }
#endif

	if(portnum==0)
	{
		ES0 = 0; // disable serial channel 0 interrupt

		PCON |= 0x80; // clock is 16x bitrate for serial0

		// set 8 bit uart with variable baud from timer 1
		// enable receiver and clear RI and TI
		SCON0 = 0x50;

	#if USE_SERIAL_INTERRUPTS
		ES0 = 1; // enable serial channel 0 interrupt
	#endif

	}
	else if(portnum==1)
	{
		ES1 = 0; // disable serial channel 1 interrupt

		WDCON |= 0x80; // clock is 16x bitrate for serial1

		// set 8 bit uart with variable baud from timer 1
		// enable receiver and clear RI and TI
		SCON1 = 0x50;

	#if USE_SERIAL_INTERRUPTS
		ES1 = 1; // enable serial channel 1 interrupt
	#endif

	}

   SetBaudCOM(portnum,PARMSET_9600);
   FlushCOM(portnum);

   EA = 1; //Enable interrupts

	return TRUE;
}

// function called by startup390 in sdcc
void Serial390Init (void) {
	OpenCOM(0, "");
}

//---------------------------------------------------------------------------
// Closes the connection to the port.
//
// 'portnum'  - number 0 to MAX_PORTNUM-1.  This number was provided to
//              OpenCOM to indicate the port number.
//
void CloseCOM(int portnum)
{
	if(portnum==0)
	{
	   ES0 = 0; //disable serial interrupt
		RI_0 = 0; // receive buffer empty
	#if USE_SERIAL_INTERRUPTS
		receiveHead0 = receiveTail0 = 0;
		transmitIsBusy0 = 0;
	   TI_0 = 0;
	#else
		TI_0 = 1; // transmit buffer empty
	#endif
	}
	else if(portnum==1)
	{
	   ES1 = 0; //disable serial interrup
	   RI_1 = 0;
	#if USE_SERIAL_INTERRUPTS
		receiveHead1 = receiveTail1 = 0;
		transmitIsBusy1 = 0;
	   TI_1 = 0;
	#else
		TI_1 = 1; // transmit buffer empty
	#endif
	}
}

//---------------------------------------------------------------------------
// Flush the rx and tx buffers
//
// 'portnum'  - number 0 to MAX_PORTNUM-1.  This number was provided to
//              OpenCOM to indicate the port number.
//
void FlushCOM(int portnum)
{
	if(portnum==0)
	{
	   ES0 = 0;
		RI_0 = 0; // receive buffer empty
	#if USE_SERIAL_INTERRUPTS
		receiveHead0 = receiveTail0 = 0;
		transmitIsBusy0 = 0;
	   TI_0 = 0;
		ES0 = 1;
	#else
		TI_0 = 1; // transmit buffer empty
	#endif
	}
	else if(portnum==1)
	{
	   ES1 = 0;
	   RI_1 = 0;
	#if USE_SERIAL_INTERRUPTS
		receiveHead1 = receiveTail1 = 0;
		transmitIsBusy1 = 0;
	   TI_1 = 0;
	   ES1 = 1;
	#else
		TI_1 = 1; // transmit buffer empty
	#endif
	}
}

//--------------------------------------------------------------------------
// Write an array of bytes to the COM port, verify that it was
// sent out.  Assume that baud rate has been set.
//
// 'portnum'  - number 0 to MAX_PORTNUM-1.  This number was provided to
//              OpenCOM to indicate the port number.
// 'outlen'   - number of bytes to write to COM port
// 'outbuf'   - pointer ot an array of bytes to write
//
// Returns:  TRUE(1)  - success
//           FALSE(0) - failure
//
int WriteCOM(int portnum, int outlen, uchar *outbuf)
{
	int i = 0;
	if(portnum==0)
	{
   	for(; i<outlen; i++)
   	{
   		serial0_putchar(outbuf[i]);
   	}
   }
   else
   {
   	for(; i<outlen; i++)
   	{
   		serial1_putchar(outbuf[i]);
   	}
   }
	return (i==outlen?TRUE:FALSE); //unnecessary, but thinking ahead
}

//--------------------------------------------------------------------------
// Read an array of bytes to the COM port, verify that it was
// sent out.  Assume that baud rate has been set.
//
// 'portnum'  - number 0 to MAX_PORTNUM-1.  This number was provided to
//               OpenCOM to indicate the port number.
// 'inlen'     - number of bytes to read from COM port
// 'inbuf'     - pointer to a buffer to hold the incomming bytes
//
// Returns: number of characters read
//
int ReadCOM(int portnum, int inlen, uchar *inbuf)
{
	volatile int i = 0;
	volatile short cnt = 5;
	volatile uchar quit = FALSE;
	if(portnum==0)
	{
   	for(; !quit && i<inlen; cnt=5)
   	{
   	   while( !serial0_peek() && (cnt--)>0 )
   	      msDelay(10);

   	   if(!serial0_peek())
   	      quit = TRUE;
   	   else
      		inbuf[i++] = serial0_getchar();
   	}
   }
   else
   {
   	for(; !quit && i<inlen; cnt=5)
   	{
   	   while( !serial1_peek() && (cnt--)>0 )
   	      msDelay(6);

   	   if(!serial1_peek())
            quit = TRUE;
         else
      		inbuf[i++] = serial1_getchar();
   	}
   }

	return i;
}

//--------------------------------------------------------------------------
// Send a break on the com port for at least 2 ms
//
// 'portnum'  - number 0 to MAX_PORTNUM-1.  This number was provided to
//              OpenCOM to indicate the port number.
//
void BreakCOM(int portnum)
{
	if(portnum==0)
	{
	   //TODO: Check this port pin
	   //hold P3.1 low
	   ES0 = 0;
      P3 &= 0xFE;
		msDelay(3);
      P3 |= 0x01;
      ES0 = 1;
   }
   else
	{
	   //hold P5.3 low
	   ES1 = 0;
		P5 &= 0xF7;
		msDelay(3);
   	P5 |= 0x08;
   	ES1 = 1;
	}
}

//--------------------------------------------------------------------------
// Set the baud rate on the com port.
//
// 'portnum'   - number 0 to MAX_PORTNUM-1.  This number was provided to
//               OpenCOM to indicate the port number.
// 'new_baud'  - new baud rate defined as
//                PARMSET_9600     0x00
//                PARMSET_19200    0x02
//                PARMSET_57600    0x04
//                PARMSET_115200   0x06
//
void SetBaudCOM(int portnum, uchar new_baud)
{
   int reload_value;
	switch (new_baud)
	{
		case PARMSET_9600:
			reload_value = BAUD9600_TIMER_RELOAD_VALUE;
			break;
		case PARMSET_19200:
			reload_value = BAUD19200_TIMER_RELOAD_VALUE;
			break;
		case PARMSET_57600:
			reload_value = BAUD57600_TIMER_RELOAD_VALUE;
			break;
		case PARMSET_115200:
			reload_value = BAUD115200_TIMER_RELOAD_VALUE;
			break;
		default:
		   return;
	}

#if USE_TIMER2_FOR_SERIAL0
   if(portnum==0)
   {
      TR2 = 0; // stop timer 2
      TL2 = RCAP2L = reload_value;
      TH2 = RCAP2H = reload_value>>8;
      TF2 = 0; // clear timer 2 overflow flag
      TR2 = 1; // start timer 2
   }
   else
   {
#else
      portnum = 0;
#endif
   	TR1 = 0; // stop timer 1
		TL1 = TH1 = reload_value;
		TF1 = 0; // clear timer 1 overflow flag
		TR1 = 1; // start timer 1
#if USE_TIMER2_FOR_SERIAL0
	}
#endif
}


//--------------------------------------------------------------------------
//  Description:
//     Delay for at least 'len' ms
//
void msDelay(int len)
{
	int i,j;
	for(i=0; i<len; i++)
		for(j=0; j<1000; j++)
			/*no-op*/;
}

//--------------------------------------------------------------------------
// Get the current millisecond tick count.  Does not have to represent
// an actual time, it just needs to be an incrementing timer.
//
long msGettick(void)
{
	return (((long)TH1)<<8)+TL1;
}

#if USE_SERIAL_INTERRUPTS
	void DS390SerialHandler0 (void)// interrupt 4
	{
		if (RI_0)
		{
			receiveBuffer0[receiveBufferHead0] = SBUF0;
			receiveBufferHead0 = (receiveBufferHead0+1)&(SERIAL_RECEIVE_BUFFER_SIZE-1);
			if (receiveBufferHead0==receiveBufferTail0) /* buffer overrun, sorry :) */
				receiveBufferTail0 = (receiveBufferTail0+1)&(SERIAL_RECEIVE_BUFFER_SIZE-1);
			RI_0 = 0;
		}
		if (TI_0)
		{
			TI_0 = 0;
			transmitIsBusy0 = 0;
		}
	}
	void DS390SerialHandler1 (void)// interrupt 7
	{
		if (RI_1)
		{
			receiveBuffer1[receiveBufferHead1] = SBUF1;
			receiveBufferHead1 = (receiveBufferHead1+1)&(SERIAL_RECEIVE_BUFFER_SIZE-1);
			if (receiveBufferHead1==receiveBufferTail1) /* buffer overrun, sorry :) */
				receiveBufferTail1 = (receiveBufferTail1+1)&(SERIAL_RECEIVE_BUFFER_SIZE-1);
			RI_1 = 0;
		}
		if (TI_1)
		{
			TI_1 = 0;
			transmitIsBusy1 = 0;
		}
	}

void serial0_putchar (char c)
{
	while (transmitIsBusy0)
		/*no-op*/;
	transmitIsBusy0 = 1;
	SBUF0 = c;
}

void serial1_putchar (char c)
{
	while (transmitIsBusy1)
		/*no-op*/;
	transmitIsBusy1 = 1;
	SBUF1 = c;
}

char serial0_getchar ()
{
	char c;
	while (receiveHead0==receiveTail0)
	   /*no-op*/;
	c = receiveBuffer0[receiveTail0];
	ES0 = 0; // disable serial interrupts
	receiveTail0++;
	ES0 = 1; // enable serial interrupts
	return c;
}

char serial1_getchar ()
{
	char c;
	while (receiveHead1==receiveTail1)
	   /*no-op*/;
	c = receiveBuffer1[receiveTail1];
	ES1 = 0; // disable serial interrupts
   receiveTail1++;
	ES1 = 1; // enable serial interrupts
	return c;
}

char serial0_peek(void)
{
   if(receiveHead0==receiveTail0)
      return FALSE;
   else
      return TRUE;
}

char serial1_peek(void)
{
   if(receiveHead1==receiveTail1)
      return FALSE;
   else
      return TRUE;
}

#else //ifdef USE_SERIALINTERRUPTS
void serial0_handler (void) interrupt 4 using 2
{
	ES0 = 0; // disable serial interrupts
}
void serial1_handler (void) interrupt 7 using 2
{
	ES1 = 0; // disable serial interrupts
}

void serial0_putchar (char c)
{
	while (!TI_0)
		/*no-op*/;
	TI_0 = 0;
	SBUF0 = c;
}

void serial1_putchar (char c)
{
	while (!TI_1)
		/*no-op*/;
	TI_1 = 0;
	SBUF1 = c;
}

char serial0_getchar (void)
{
   char c;
	while (!RI_0)
		/*no-op*/;
	c = SBUF0;
	RI_0 = 0;
	return c;
}

char serial1_getchar (void)
{
   char c;
	while (!RI_1)
		/*no-op*/;
	c = SBUF1;
	RI_1 = 0;
	return c;
}

char serial0_peek(void)
{
   if(RI_0)
      return TRUE;
   else
      return FALSE;
}

char serial1_peek(void)
{
   if(RI_1)
      return TRUE;
   else
      return FALSE;
}

#endif // ifdef USE_SERIALINTERRUPTS

#if __C51__
//All that should be necessary is a macro, but not
//with Keil...

//Keil C51 v5.10 uses non-standard expression for putchar
char putchar(char c)
{
   serial1_putchar(c);
   return c;
}

char getchar(void)
{
   return serial1_getchar();
}
#endif
