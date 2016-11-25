/*
The MIT License (MIT)

Copyright (c) 2016 Madis Kaal <mast@nomad.ee>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#include "keypad.hpp"
#include "lcd.hpp"

#define TICKCOUNT 0xc0

// frequency sweep sync output pin set/reset
#define SYNC_HIGH() (PORTB|=4)
#define SYNC_LOW() (PORTB&=~4)

LCD display;
KeyPad keypad;

// sine table for AM modulation
uint8_t am_sintable[32] = {
  128,153,177,199,218,234,245,253,
  255,253,245,234,218,199,177,153,
  128,103,79 ,57 ,38 ,22 ,11 ,3  ,
  1  ,3  ,11 ,22 ,38 ,57 ,79 ,103
};

int32_t fm_sintable[32] = {
  0L    ,14631L,28701L,41667L,53033L,62360L,69290L,73558L,
  75000L,73558L,69290L,62360L,53033L,41667L,28701L,14631L,
  0L    ,-14631L,-28701L,-41667L,-53033L,-62360L,-69290L,-73558L,
  -75000L,-73558L,-69290L,-62360L,-53033L,-41667L,-28701L,-14631L
};

uint32_t ftable[32];

typedef union 
{
  uint64_t u64;
  uint32_t halves[2];
} Z;

class DDS
{
public:
  DDS() {}
  
  #define RESET_HIGH() (PORTC|=2)
  #define RESET_LOW() (PORTC&=~2)
  #define WCLK_HIGH() (PORTC|=1)
  #define WCLK_LOW() (PORTC&=~1)
  #define FQUD_HIGH() (PORTB|=1)
  #define FQUD_LOW() (PORTB&=~1)
  
  void reset()
  {
    RESET_HIGH();
    _delay_ms(1);
    RESET_LOW();
    WCLK_LOW();
    FQUD_LOW();
  }
  
  void write(uint8_t b)
  {
    PORTD=b;
    WCLK_HIGH();
    WCLK_LOW();
  }
  
  // write value to DDS
  void setvalue(uint32_t value)
  {
    FQUD_LOW();
    write(0x00);
    write(value>>24);
    write(value>>16);
    write(value>>8);
    write(value);
    FQUD_HIGH();
    PORTD=0; // keep data bus low for quick keypad checks
  }

  // calculate value to write into DDS
  // this is quite slow
  uint32_t calcvalue(uint32_t f)
  {
    Z z;
    // if wanted frequency is over half the clock frequency
    // then aim the low alias (resulting from mixing the DDS output
    // with DDS clock frequency) at the wanted frequency instead
    if (f>62500000)
      f=125000000-f;
    z.halves[1]=f; // quicker and faster way to multiply with
    z.halves[0]=0; // 2**32
    // the module has 125MHz clock
    return (uint32_t)(z.u64/125000000L);
  }
   
  void setfrequency(uint32_t f)
  {
    setvalue(calcvalue(f));
  }
  
  
};

DDS dds;

void fset(const char* name,int32_t value)
{
 display.clear();
 display.prints(name);
 display.printc('=');
 display.printn(value);
 display.prints("\r\n");
 dds.setfrequency(value);
}

ISR(TIMER0_OVF_vect)
{
  // reset timer for next interrupt
  TCNT0=TICKCOUNT;
}

ISR(WDT_vect)
{
}

void error(const char* msg)
{
  display.clear();
  keypad.flush();
  display.prints(msg);
  while (!keypad.ready()) {
    wdt_reset();
    WDTCSR|=0x40;
    keypad.scan();
  }
  keypad.flush();
}

/*
I/O configuration
-----------------
I/O pin                               direction     DDR  PORT
PC0 DDS write clk                     output        1    1
PC1 DDS reset                         output        1    1
PC2 keypad input                      input         0    1
PC3 keypad input                      input         0    1
PC4 keypad input                      input         0    1
PC5 keypad input                      input         0    1

PD0 data0                             output        1    0
PD1 data1                             output        1    0
PD2 data2                             output        1    0
PD3 data3                             output        1    0
PD4 data4                             output        1    0
PD5 data5                             output        1    0
PD6 data6                             output        1    0
PD7 data7                             output        1    0

PB0 dds FQ_UD                         output        1    0
PB1 AM modulation sine output         output        1    0
PB2 sweep sync out                    output        1    0
PB3 LCD R/S                           output        1    0
PB4 LCD E                             output        1    0
PB5 unused                            output        1    0
*/
int main(void)
{
uint16_t i;
uint8_t j;
  MCUSR=0;
  MCUCR=0;
  // I/O directions
  DDRC=0x03;
  DDRD=0xff;
  DDRB=0x3f;
  // initial state
  PORTC=0x3f;
  PORTD=0x00;
  PORTB=0x00;
  //
  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_enable();
  // configure watchdog to interrupt&reset, 4 sec timeout
  WDTCSR|=0x18;
  WDTCSR=0xe8;
  // configure timer0 for periodic interrupts
  TCCR0B=5; // timer0 clock prescaler to 1024
  TIMSK0=1; // enable overflow interrupts
  TCNT0=TICKCOUNT;
  display.reset();
  display.cursoronoff(1);
  sei();
  int32_t f=0,fa=1000000L,fb=9000000L,fc,fd;
  dds.reset();
  fset("FA",fa);
  while (1) {
    sleep_cpu(); // timer interrupt wakes us up
    wdt_reset();
    WDTCSR|=0x40;
    keypad.scan();
    if (keypad.ready()) {
      char c=keypad.getch();
      if (c>='0' && c<='9')
      {
        f=f*10+(c-'0');
        display.printc('\r');
        display.printn(f);
      }
      else {
        switch (c) {
          case 'D':
            f=f/10;
            display.printc('\r');
            if (f)
              display.printn(f);
            display.prints(" \b");
            break;
          case '#':
            if (fa<75000) {
              error("Unable <75KHz");
            }
            else {
              fset("FC",fa);
              display.prints("Modulation: FM");
              j=0;
              for (j=0;j<32;j++) {
                ftable[j]=dds.calcvalue(fa+fm_sintable[j]);
              }
              // make sure all keys are released before
              // beginning because keypad scans are done
              // very frequently
              while (keypad.readall()) {
                wdt_reset();
                WDTCSR|=0x40;
              }
              cli();
              while (1) {
                wdt_reset();
                WDTCSR|=0x40;
                dds.setvalue(ftable[j++]);
                j&=(COUNTOF(ftable)-1);
                _delay_us(11);
                if (keypad.pressed())
                  break;
              }
              sei();
            }
            while (keypad.pressed()) {
              wdt_reset();
              WDTCSR|=0x40;
            }
            keypad.flush();
            fset("FA",fa);
            f=0;
            break;
          case '*':
            fset("FC",fa);
            f=dds.calcvalue(fa);
            display.prints("Modulation: AM");
            j=0;
            while (keypad.readall()) {
              wdt_reset();
              WDTCSR|=0x40;
            }
            TCCR1A=0x81; 
            TCCR1B=0x09; // clock prescaler 1
            OCR1AH=0;
            cli();
            while (1) {
              wdt_reset();
              WDTCSR|=0x40;
              dds.setvalue(f);
              OCR1AL=am_sintable[j++];              
              j&=(COUNTOF(am_sintable)-1);
              _delay_us(12);
              if (keypad.pressed())
                break;
            }
            sei();
            TCCR1A=0;
            PORTB&=~2;
            while (keypad.readall())
            {
              wdt_reset();
              WDTCSR|=0x40;
            }            
            keypad.flush();
            fset("FA",fa);
            f=0;
            break;
          case 'A':
            if (f>124999900L || f<0)
              error("Unable, must be\r\n0..124999900");
            else
              fa=f;
            fset("FA",fa);
            f=0;
            break;
          case 'B':
            if (f>124999900L || f<0)
              error("Unable, must be\r\n0..124999900");
            else
              fb=f;
            fset("FB",fb);
            f=0;
            break;
          case 'C':
            fc=(fb-fa);
            if (fc<0) {
              fc=0-fc;
              f=fb;
              fb=fa;
              fa=f;
            }
            display.clear();
            display.prints("FA=");
            display.printn(fa);
            display.prints("\r\n");
            display.prints("FB=");
            display.printn(fa+fc);
            while (!keypad.ready()) {
              wdt_reset();
              WDTCSR|=0x40;
              SYNC_HIGH();
              fd=0;
              for (i=0;i<256;i++) {
                f=fa+(fd>>8);
                fd+=fc;
                dds.setfrequency(f);
                _delay_us(500);
              }
              SYNC_LOW();
              keypad.scan();
              _delay_ms(10);
              dds.setfrequency(fa);
              _delay_ms(10);
              keypad.scan();
            }
            keypad.flush();
            fset("FA",fa);
            f=0;
            break;
        }
      }
    }
  }
}
