typedef bool(* CommandHandler)(void);

struct CommandMessage {
  char            cmd[3];
  CommandHandler  hdlr;
};

const struct CommandMessage CmdMsgTable[] {
  {"ms", ModeScrollHdlr},
  {"mf", ModeFreezeHdlr},
  {"mv", ModeFFTHdlr},
  {"it", ToggleInvertHdlr},
  {"ss", SetSpeedHdlr},
  {"rs", ResetHdlr},
  {"tg", TimeGetHdlr},
  {"ts", TimeSetHdlr},
  {"tt", TimeToggleHdlr},
  {"rt", RotateToggleHdlr},
  {"ds", DimSetHdlr},
  {"mb", ModeBinaryHdlr},
  {"sv", ShowVanityHdlr},
};

const int CmdMsgTableSize = (sizeof(CmdMsgTable)/sizeof(CommandMessage));



//returns true if there was input
bool InterpretCommands() {
  //if there's no serial, just leave
  if (!(Serial.available())) return false;

  //if a command character is entered;
  if ('`'==Serial.peek()) {
      //get rid of the command character
      Serial.read();

      //catch the command characters
      char cmdchar[2];
      cmdchar[0]=Serial.read();
      cmdchar[1]=Serial.read();

      //get the handler for this command
      CommandHandler func = GetCommandHandler(cmdchar);

      //execute it
      if (func) func();
    }

  //otherwise just pull in the message
  else {
    String passmsg="";
    while (Serial.available()) passmsg+=(char)Serial.read();
    updateMessage(passmsg);
    if (mode==MODE_FREEZE) copyMessageIntoScreen();
    currframe=0;
    msgChangeFlag=true;
  }
  //clearBuffer();
  Serial.write(0xFF);
  return true;
}



CommandHandler GetCommandHandler(char cmdchar[]) {
  //iterate through the msgtable
  for (byte i = 0; i<CmdMsgTableSize; i++) {
    
    //compare both command characters to the message table inputs
    if ((cmdchar[0]==pgm_read_byte(&CmdMsgTable[i].cmd[0]))&(cmdchar[1]==pgm_read_byte(&CmdMsgTable[i].cmd[1]))) {
      CommandHandler hdlr = (CommandHandler) pgm_read_word(&CmdMsgTable[i].hdlr);
      return hdlr;
    }
  }
  return NULL;
}



bool SetSpeedHdlr() {
  Serial.read(); //get rid of the space
  int input=0;
  while (isDigit((char) Serial.peek())) {
    input*=10;
    input+=(int) Serial.parseInt();
  }
  btPnP= input;
  while (Serial.available()) Serial.read();  
  return true;
}



bool ToggleInvertHdlr() {
  invert=!invert;
  while (Serial.available()) Serial.read();
  return true;
}


bool ModeFFTHdlr() {
  mode=MODE_FFT;
  clearBuffer();
  while (Serial.available()) Serial.read();
  return true;
}

bool ModeBinaryHdlr() {
  mode=MODE_BIN;
  clearBuffer();
  while (Serial.available()) Serial.read();
  return true;
}

bool ModeScrollHdlr() {
  mode=MODE_SCROLL;
  currframe=0;
  while (Serial.available()) Serial.read();
  clearBuffer();
  return true;
}

bool ResetHdlr() {
  while (Serial.available()) Serial.read();
  setup();
}

bool TimeToggleHdlr() {
  while (Serial.available()) Serial.read();
  clockdisplay=!clockdisplay;
}

bool RotateToggleHdlr() {
  while (Serial.available()) Serial.read();
  screenRotated=!screenRotated;
}



bool ModeFreezeHdlr() {

  //dictate future behaviour
  mode=MODE_FREEZE;
  
  //copy as much of the message into the buffer as possible
  for (int i=0; i<108; i++) {
    updateBuffer(i);
  }
  
  copyMessageIntoScreen();

  while (Serial.available()) Serial.read();
  return true;
}

bool TimeGetHdlr() {
  if (hour()<10) Serial.print('0');
  Serial.print(hour());
  Serial.print(":");
  if (minute()<10) Serial.print('0');
  Serial.print(minute());
  Serial.print(" ");
  Serial.print(day());
  Serial.print("-");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year());

  while (Serial.available()) Serial.read();
}

bool TimeSetHdlr() {
  Serial.read();//get rid of space
  int hr = Serial.parseInt();
  int mnt = Serial.parseInt();
  //seconds
  int dy = Serial.parseInt();
  int mnth = Serial.parseInt();
  int yr = Serial.parseInt();
  setTime(hr,mnt,0,dy,mnth,yr);
  Teensy3Clock.set(now());
  
  updateDateMessage();
  
  while (Serial.available()) Serial.read();
}

bool DimSetHdlr() {
  Serial.read();
  int val = Serial.parseInt();
  //val%=257;
  analogWrite(TBlnP,val);
  analogWrite(BBlnP,val);
  while (Serial.available()) Serial.read();
}

bool ShowVanityHdlr() {
  Serial.println("Twinvision LED Signage Driver");
  Serial.println("Programming and reverse engineering by Bo Thompson");
  Serial.print("Compiled on ");
  Serial.println(__DATE__);
  
  while (Serial.available()) Serial.read();
}
