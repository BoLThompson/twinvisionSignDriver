/*
 *  THE THEORY OF THIS TWINVISION BUS SIGN
 *  Basically it's two grids of 7x108 LEDs. both grids are controlled by 48 tlc5921 chips.
 *  As far as control goes, the two grids get separate signal inputs but they share clock and latch pins.
 *  There are also Top- and Low- blanking pins that allow us to disable either or both grids at any time.
 *  You could even PWM these to adjust brightness.
 *  In order to push large amounts of bits across the screen without leaving ghost trails, you need to raise the latch pin whenever you're writing data.
 *  Yes I did in fact mean raise the latch pin. Some inputs are fed into an odd number of inverters and we're dealing with negative logic in this case.
 *  There's also some kind of photodiode kind of thing that outputs a PWM signal describing the ambient light in front of the sign, but I have pretty much no use for that.
 *  
 *  The logic of the clock, latch, and two signal pins are all interpreted with respect to a 'logical bias voltage' that is fed to the sign on two pins.
 *  This bias voltage is fed into the four comparators of a LM2091MX. One pin goes to the noninverting inputs of comparators one and two (where the inverting inputs receive Xlat and SClk,
 *  one pin goes to the inverting inputs of comparators three and four (where the noninverting inputs receive TSig and BSig). Why does the bias voltage go to the inverting outputs
 *  of one pair of signals and the noninverting outputs of the other pair? No idea.
 *  Because of this bias voltage, I think that a 3v3 to 5v logic level shifter is not needed to make low voltage devices talk to the sign. I think it's actually quite flexible. In practice
 *  I'm actually running this on a Teensy3.6 (3v3 logic) with no observed trouble; I just need to make sure that the logic bias voltage is around 2v or so.
 *  
 *  The two grids are arranged one on top of the other so that the full thing is 14x108 LEDs.
 *  Ergo, in order to write a character across both grids, we need to write the top half and bottom half to both grids simultaneously.
 *  Remember that moving text approaches from the right and to the left, so we need to write the leftmost colum of a character first.
 *  
 *  Characters in this program are stored as arrays of bytes, where 0 represents an 'off' LED and 1 is 'on'.
 *  The first byte is the leftmost column of the top half of the character. The second half of the array is the lower half of the character.
 *  That just kind of struck me as the most elegant solution but it doesn't make it very easy to design the byte arrays for each character.
 *  
 *  Worth mentioning is that the sign is actually divided into three separate PCBs. The last driver chip of each grid on each pcb has four legs not
 *  attached to any LED. You need to account for this in each frame of animation by pushing four extra bits at a precalculated position twice.
 *  This is already handled in the program below using writePCBDivSpace().
 */

//needed for the clock
#include <TimeLib.h>

//begin cargo code===========================
#include <arduinoFFT.h>

arduinoFFT FFT= arduinoFFT();

#define CHANNEL1 A0
#define CHANNEL2 A1 //lol stereo FFT
const uint16_t samples = 256; //This value MUST ALWAYS be a power of 2
const double samplingFrequency = 9999; //Hz, must be less than 10000 due to ADC

unsigned int sampling_period_us;
unsigned long microseconds;

double vReal[samples];
double vImag[samples];

#define SCL_INDEX 0x00
#define SCL_TIME 0x01
#define SCL_FREQUENCY 0x02
#define SCL_PLOT 0x03

#define sampling_period_us round(1000000*(1.0/samplingFrequency))
//end cargo code=============================


#define BBlnP 2   //Raise this to enable the bottom half display (lowers the LOBLANK pin)
#define TBlnP 3   //Raise this to enable the top half display (lowers the HIBLANK pin)
#define BSigP 4   //Raise this to send ones to bottom chips
#define TSigP 5   //Raise this to send ones to top chips
#define SClkP 6   //Raise this to lower SCLCK on the chips
#define XLatP 7   //Raise this to lower XLAT on the chips.



//pauses
//#define btSnC 1 "between signal and clock" is no longer needed now that I'm using the logic bias pin correctly. woot for the bandwidth gain.
#define btCnC 1   //if you take this to zero, the clock pulses too fast for the sign to consistenly notice.
int btPnP;        //this is the amount of milliseconds that the driver waits for after updating the sign. lower value means faster scrolling.



//hardware
//this is all just for me really. If you need to adapt this to a different sign you'll probably need to do a lot more than just adjust these constants.
#define buffLen 108
#define pcbWidth 36
#define firstDiv buffLen-pcbWidth*2
#define secondDiv buffLen-pcbWidth



//animation
int currframe;                //used to keep track of how many pixels we've scrolled the message.
int msgcol;                   //total number of columns in the message. needs to be recalculated every time the message changes.
byte upperBuff[buffLen];      //the buffer that's going to be copied into the upper grid of the sign
byte lowerBuff[buffLen];      //""
bool invert;                  //if this is toggled, the buffer is NOT'd into the sign to make dark text scroll across a lit background.
bool msgChangeFlag;           //used to keep track of whether the message just changed.

//command stuff
typedef bool(* CommandHandler)(void);   //fuck the arduino IDE for making me declare this here and not in the commands tab

//mode changing stuff
#define MODE_SCROLL   0
#define MODE_FREEZE   1
#define MODE_FFT      2
#define MODE_BIN      3
int mode;                     //should only ever be set to one of the above constants



//this effects whether the buffer is copied to the screen forwards or upside down and backwards -- like rotated 180 degrees.
//I set this up to rotate the entire screen 180 degrees so that I could run wires through a hole in the top of the case and not drill a new hole
bool screenRotated;


//message stuff
#define msgMax 256        //maximum length of the message the sign displays. Chosen arbitrarily, can go much higher
                          //note: currently a message longer than the driver's uart buffer is not handled properly. You could theoretically have thousands of characters in your message, but
                          //under my current implementation you'd have no way to feed them in after compile.
int msglength {0};
bool clockdisplay;        //toggles whether the clock is visible during modes which might display it
int clockhour {0};        //12-hour format hour


//this struct just holds the pointer to a byte array for a character and the size of the array it points to. just a really simple vector.
struct structCharVector {
  const byte *msg;
  byte size;
};

struct structCharVector message[msgMax];      //array of structCharVectors which represents the message to be displayed. this gets broken down further before it's copied to the screen.
byte dateMsg[32];                             //array of regular-ass bytes that stores the pattern which represents the current time. this optionally gets overlayed onto lowerBuff[].

extern int charmapSize;                       //seriously why do I have to declare this here fuck
struct charmapStruct {
  char          input;
  structCharVector    content;                //output was a reserverd word. what the hell does that even do?
};
struct datemapStruct {                        //this exists only so that I can create an array of pointers to the byte patterns which represent my 0-9 digits that show on the clock.
  const byte    *content;                     //i think there's a much easier way to make an array of pointers but idgaf this worked
};
extern struct charmapStruct charmap[];        //import the list of characters and how each one translates to one of my byte patterns
extern struct datemapStruct datemap[];        //same for the 0-9 clock digits

//bool serialconstat = false;                   //exists as a vanity thing. by setting this variable I can see if you've just connected to the USB port and I can send you my nerd cred message.







time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}







//finally it's time for some god damned functions
void setup() {  
  setSyncProvider(getTeensy3Time);
  
  //time to hang on each frame of the scolling animaiton
  btPnP=30;

  //keeps track of how many pixels the sign has scrolled
  currframe=0;

  //enables the rtc display
  clockdisplay=true;

  //number of columns of pixels in the current message (initialized to zero but recalculated for each new message)
  msgcol=0;

  //1=led on, 0=led off
  invert=false;

  //keeps track of whether the last serial input changed the message
  msgChangeFlag=false;

  //toggles whether the sign is feed directly from the screen buffer, or fed backwards (for 180 degree flipping)
  screenRotated=false;

  //scroll the message along as opposed to keeping it on screen or other modes
  mode=MODE_SCROLL;

  //output setting
  pinMode(SClkP, OUTPUT);       //signal clock
  pinMode(XLatP, OUTPUT);       //signal latch
  pinMode(TSigP, OUTPUT);       //top signal
  pinMode(BSigP, OUTPUT);       //bottom signal
  pinMode(BBlnP, OUTPUT);       //bottom blanking
  pinMode(TBlnP, OUTPUT);       //top blanking
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(SClkP, HIGH);    //clock is preset to high but not sure
  digitalWrite(XLatP, HIGH);    //latch is preset to high*
  digitalWrite(TSigP, LOW);     //top signal preset to high but not sure about this
  digitalWrite(BSigP, LOW);     //bottom signal again*
  digitalWrite(LED_BUILTIN, HIGH);
  
  analogWrite(BBlnP,255);    //high means enabled
  analogWrite(TBlnP,255);

  //comm line
  Serial.begin(115200);

  //initialize the buffer with zeroes
  clearBuffer();

  //clear out all the static by pushing the zeroed out buffer onto the screen
  writeBuffToDisplay(0);

  //and initialize to our friendly business tagline
  updateMessage("Respectech Inc.              Bridging the Gap Between People and Technology");
}



//zeroes out the display buffer
void clearBuffer() {
  for (int ii=0; ii<buffLen; ii++) {
    upperBuff[ii]=0;
    lowerBuff[ii]=0;
  }
}



//calculates and saves the number of columns of pixels in the current message
void updateMsgcol() {
  msgcol=0;
  for (int ii=0; ii<msglength; ii++){
    msgcol+=getCharSize(&message[ii]);
  }
}



//actually writes a pair of bits into the sign (one on top field and one on bottom field)
void writeBit(bool tbit, bool bbit) {    
  //possibly raise the signal pins
  digitalWriteFast(TSigP,tbit);
  digitalWriteFast(BSigP,bbit);
  //delayMicroseconds(1);
  //if (tbit) PORTD |= _BV(TSigP);
  //if (bbit) PORTD |= _BV(BSigP);
    
  //pulse the clock pin
  digitalWriteFast(SClkP,LOW);
  //PORTD &= ~_BV(SClkP);
  delayMicroseconds(btCnC);
  digitalWriteFast(SClkP,HIGH);
  //PORTD |= _BV(SClkP);

  delayMicroseconds(1);
  
  //lower the sin pins
  digitalWriteFast(TSigP,LOW);
  digitalWriteFast(BSigP,LOW);
  //PORTD &= ~(_BV(TSigP) | _BV(BSigP));

  //delayMicroseconds(1);

}



//copies the columns of a character in the message into the buffer. note that this function does not wipe the entire message.
void updateBuffer(int offSet) {
  
  //if the offSet is greater than the number of columns in the message (as in, this column has no text to display)
  if (offSet>=msgcol){
    
    //fill this column with zeroes
    upperBuff[offSet%buffLen]=0;
    lowerBuff[offSet%buffLen]=0;
  }

  //otherwise
  else{
    //starting from the first character,
    int charoffset=offSet;
    int currchar=0;

    //subtract the width of each character in the message from our total offset until our offset is less than the current character's width.
    while (charoffset>(message[currchar].size>>1)-1) {

      //subtract that character's width from the total offset
      charoffset-=message[currchar].size>>1;

      //and examine the next character
      currchar++;
    }

    //half of the current char's array size is its width
    int charWidth=message[currchar].size>>1;

    //write in the current character's current column to the current column of the message buffer.
    upperBuff[offSet%buffLen]=message[currchar].msg[charoffset];
    lowerBuff[offSet%buffLen]=message[currchar].msg[charoffset+charWidth];
    }
}



//takes an interger as offset. Message is copied onto the screen but shifted to the left by offset amount of pixels.
void writeBuffToDisplay(int offSet) {
  int incdir;
  int stopp1;
  int stopp2;
  int stopp3;
  int startp;
  
  if (screenRotated) {
    incdir=-1;              //count downwards
    startp=buffLen-1;       //begin at the last column of the screen
    stopp1=secondDiv-1;     //insert a pcb division here
    stopp2=firstDiv-1;      //and here
    stopp3=-1;              //and stop after writing the 0th column
  }
  else {
    incdir=1;               //count upwards
    startp=0;               //start on the 0th column
    stopp1=firstDiv;        //insert a pcb division here
    stopp2=secondDiv;       //and here
    stopp3=buffLen;         //and stop after writing the final column
  }

  //first third of the bytes
  for (int ii=startp; ii!=stopp1; ii+=incdir) {
    prepareBuffColumn(ii+offSet);
  }
  writePCBDivSpace();

  //and again
  for (int ii=stopp1; ii!=stopp2; ii+=incdir) {
    prepareBuffColumn(ii+offSet);
  }
  writePCBDivSpace();

  //and again.
  for (int ii=stopp2; ii!=stopp3; ii+=incdir) {
    prepareBuffColumn(ii+offSet);
  }
}


//iirc any function that calls this would originally just call writeBuffColumn directly, and I wrote this to intercept those calls and overlay the date pattern. kind of hacky.
void prepareBuffColumn(int offSet) {
  int column=(offSet)%buffLen;
  byte tline=upperBuff[column];
  byte bline=lowerBuff[column];
  int datewidth {17};
  if (clockhour<10) datewidth-=3;
  
  if (invert) {
    tline=~tline;
    bline=~bline;
  }

  //figure out which column of the physical display this is
  int displaycolumn=offSet-currframe;

  //if this is left of the 33rd column,
  if ((displaycolumn<datewidth) and (mode==MODE_SCROLL) and (clockdisplay==true)) {
  
    //blank off the bottom 5 pixels
    bline &= B11100000;
  
    //fill em in with the date pattern
    byte datepattern=dateMsg[displaycolumn];
    if (invert) {
      datepattern=~datepattern;
      datepattern&=B00011111;
    }
    bline |= datepattern;
    }
  writeBuffColumn(tline,bline);
}



//dumps a given low byte and high byte to the sign
void writeBuffColumn(byte tline, byte bline) {
  if (!screenRotated) {
    for(byte iii=0; iii<7; iii++) {
      writeBit((tline & 1),(bline & 1));
      tline=tline>>1;
      bline=bline>>1;
      }
  }
  else {
    for(byte iii=0; iii<7; iii++) {
      writeBit((bline & B01000000),(tline & B01000000));
      tline=tline<<1;
      bline=bline<<1;
      }
  }
}


//accounts for the eight absent LEDs at the end of each PCB.
inline void writePCBDivSpace() {
  writeBit(0,0);
  writeBit(0,0);
  writeBit(0,0);
  writeBit(0,0);
}


//gets the number of columns of a given structCharVector, which is half its size
int getCharSize(const struct structCharVector *c) {
  int charWidth=c->size;
  return(charWidth>>1);
}



inline void toggleLatch(bool n){
  digitalWriteFast(XLatP,n);
  //if (n) PORTB |= (_BV(0));
  //else PORTB &= ~(_BV(0));
  //this is a really fucking stupid function but for some reason you aren't allowed to just XOR to PORTD on the teensy so
}



//I really love the modulo operator
void updateDateMessage() {
  for(int ii=0; ii<32; ii++) {
    dateMsg[ii]=0;
  }
  int currdatecol=0;
  
  clockhour = hour(); //get the 24 hour hour (0 = midnight)
  clockhour+=11;          //add 11 to make 0 become 11    and 1 becomes 12
  clockhour%=12;          //mod 12 to make 11 become 11    and 12 becomes 0
  clockhour++;            //add one to make 11 become 12 and 12 becomes 1. 0 became 12, and 1 became 1. Now we're in the 12 hour format.

  //first digit of the hour, but just skip if it's a zero.
  if (clockhour>=10) {
    for (int i=0;i<4;i++){
      dateMsg[currdatecol]=datemap[(byte) floor(clockhour/10)].content[i];
      currdatecol++;
    }
  }
  else {
    //otherwise just skip over the space for that digit
    currdatecol++;
  }
  
  //second digit of the hour
  for (int i=0;i<4;i++){
    dateMsg[currdatecol]=datemap[(byte) (clockhour%10)].content[i];
    currdatecol++;
  }

  dateMsg[currdatecol]=B00001010; //the colon
  currdatecol++;
  dateMsg[currdatecol]=0; //the space after the colon
  currdatecol++;

  //first minute digit
  for (int i=0;i<4;i++){
    dateMsg[currdatecol]=datemap[(byte) floor(minute()/10)].content[i];
    currdatecol++;
  }

  //second minute digit
  for (int i=0;i<4;i++){
    dateMsg[currdatecol]=datemap[(byte) minute()%10].content[i];
    currdatecol++;
  }
}


//takes a string and maps each character to one of my bytemap character representations.
void updateMessage(String input) {
  
  //message length = 0, we're going to use this to count up the string
  msglength = 0;
  
  //for each character in input
  for (int i=0; (i<input.length() & msglength<msgMax); i++) {
    
    //iterate through the charmap to find the character that this is
    for (int ii=0; ii<charmapSize; ii++) {
      
      //once we find a match
      if (input.charAt(i)==charmap[ii].input) {
        //we set the current message[] character and size
        message[msglength].msg=charmap[ii].content.msg;
        message[msglength].size=charmap[ii].content.size;
        
        msglength+=1;
        
        //and break
        break;
      }
    }  
  }
  updateMsgcol();
}


//pretty sure this is deprecated
void copyMessageIntoScreen() {
  //copy as much of the message into the buffer as possible
  for (int i=0; i<buffLen; i++) {
    updateBuffer(i);
  }
  
  //update sign
  toggleLatch(0);
  
  //buffer begins at the far right side, displaying the full message
  writeBuffToDisplay(0);
  toggleLatch(1);
}



void ModeScrollStep() {
  //update our buffer
  updateBuffer(currframe);

  //turn off the display, write the buffer onto it, and turn it back on
  toggleLatch(0);
  writeBuffToDisplay(currframe+1);
  toggleLatch(1);

  //hold long enough to display the message nicely
  delay(btPnP);

  //update the clock sometimes
  if (currframe%buffLen ==0) {
    updateDateMessage();
  }
  
  //move to the next frame
  currframe++;
  
  if (currframe > buffLen+msgcol) {
    currframe=0;
  }
  
//  if (serialconstat) {
    InterpretCommands();
    if (msgChanged()) clearBuffer();
//  }
}



bool msgChanged() {
  if (msgChangeFlag==true) {
    msgChangeFlag=false;
    return true;
  }
  return false;
}



void ModeFreezeStep() {

  delay(btPnP);
//  if (serialconstat) {
    if (InterpretCommands()) {
        toggleLatch(0);
        writeBuffToDisplay(0);
        toggleLatch(1);
    }
//  }
}



void loop() {
  switch (mode) {
    case MODE_SCROLL:
      ModeScrollStep();
      break;
    case MODE_FREEZE:
      ModeFreezeStep();
      break;
    case MODE_FFT:
      ModeFFTStep();
      break;
    case MODE_BIN:
      ModeBinaryStep();
      break;
  }
}




void ModeBinaryStep(){  
  //for the bufferlength
  for (int i=0; i<buffLen; i++) {
    
    //wait for serial
    while(!Serial.available());
    
    //set upperbuff to a byte read from serial
    upperBuff[i]=(byte) Serial.read();
    
    //wait for serial
    while(!Serial.available());
    
    //set lowerbuff to a byte read from serial
    lowerBuff[i]=(byte) Serial.read();
  }

  //display our new image
  toggleLatch(0);
  writeBuffToDisplay(0);
  toggleLatch(1);
  
  //wait for serial
  while(!Serial.available());
  
  //interpret the control character
  byte incode = Serial.read();

  //make sure that this control character starts with a one, or it's something weird.
  if ((incode&B10000000)==0) {
    mode=MODE_SCROLL;
    clearBuffer();
    while(Serial.available()) Serial.read();
    currframe=0;
  }

  switch (incode) {
    case B10000001:
      invert^=1;
      break;
  }
  
  //send the "I'm ready" code
  Serial.write(0xFF);
}



void ModeFFTStep()
{
  /*SAMPLING*/
  for(int i=0; i<samples; i++)
  {
      microseconds = micros();    //Overflows after around 70 minutes!

      vReal[i] = (analogRead(CHANNEL1)>>1)+(analogRead(CHANNEL2)>>1);
      vImag[i] = 0;
      while(micros() < (microseconds + sampling_period_us)){
        //empty loop
      }
  }
  FFT.Compute(vReal, vImag, samples, FFT_FORWARD); // Compute FFT 
  FFT.ComplexToMagnitude(vReal, vImag, min(samples,buffLen)); // Compute magnitudes 
  
  for(int i=0; ((i<buffLen)&(i<samples)); i++) {
    byte out=0;
    /*if (i%2==0) */out = (byte) floor(sqrt(vReal[(i)+1])-2)/3;
    upperBuff[i]=0;
    lowerBuff[i]=0;
    out=min(out,14);
    //Serial.println(out);
    //set another 1 for each value of out
    while ((out>0) & ((lowerBuff[i]&B01000000)==0)) {
      lowerBuff[i]=lowerBuff[i]<<1;
      lowerBuff[i]|=0x01;
      out--;
    }
    while (out>0) {
      upperBuff[i]=upperBuff[i]<<1;
      upperBuff[i]|=0x01;
      out--;
    }
  }
  toggleLatch(0);
  writeBuffToDisplay(0);
  toggleLatch(1);
  
  if (InterpretCommands()==true) clearBuffer();
}
