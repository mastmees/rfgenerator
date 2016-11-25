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
#ifndef __keypad_hpp__
#define __keypad_hpp__
#include <avr/io.h>

#ifndef COUNTOF 
#define COUNTOF(x) (unsigned int)(sizeof(x)/sizeof(x[0]))
#endif

// 4 columns on PD4,PD5,PD6,PD7
// 4 rows on PC2,PC3,PC4,PC5
class KeyPad
{
  uint8_t buttons[4*4];
  uint8_t buf[8];
  volatile uint8_t head,tail,count;
  const char *ktable;
    
  void putc(uint8_t c)
  {
    if (count<COUNTOF(buf)) {
      buf[head]=c;
      head=(head+1)%COUNTOF(buf);
      count++;
    }
  }

  uint8_t read_col(uint8_t c)
  {
    uint8_t r;
    PORTD&=(~(0x10<<c));
    _delay_us(2);
    r=PINC>>2;
    PORTD|=(0x10<<c);
    return r;
  }
    
public:
  KeyPad(): head(0),tail(0),count(0),ktable("A321B654C987D#0*")
  {
    memset(buttons,0,sizeof(buttons)); 
  }

  // this assumes that all columns are held low  
  bool pressed()
  {
    return ((PINC&0x3c)!=0x3c);
  }
  
  // brings all columns low and checks if any key is pressed
  bool readall()
  {
    PORTD&=0x0f;
    _delay_us(2);
    return pressed();
  }
  
  // call this periodically to scan keypad for input
  // for each 4 columns set the output to outputmode, and drive low
  // then read each row and shift the status into respective button
  // in state matrix
  void scan()
  {
    uint8_t *ptr=buttons,r,c,b;
    for (c=0;c<4;c++) {
      r=read_col(c);
      for (b=0;b<4;b++) {
        *ptr=(*ptr<<1)|(r&1);
        r=r>>1;
        ptr++;
      }
    }
    // examine the state matrix to see which buttons just went
    // into pressed state, and push characters to keypad read buffer
    ptr=buttons;
    for (c=0;c<COUNTOF(buttons);c++)
    {
      if ((*ptr&0x1f)==0x10) // button counts as pressed if it has
        putc(ktable[c]);     // not bounced in last 4 scans
      ptr++;
    }
  }
  
  // return 0 if no input in buffer, otherwise return char
  uint8_t getch()
  {
    uint8_t c=0;
    if (count) {
      c=buf[tail];
      tail=(tail+1)%COUNTOF(buf);
      count--;
    }
    return c;
  }
  
  bool ready() { return (count!=0); }
  
  void flush()
  {
    head=tail=count=0;
  }
  
};
#endif
