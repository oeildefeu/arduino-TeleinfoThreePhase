/* Auteur : Snips, sur https://easydomoticz.com/forum/viewtopic.php?t=158&start=130
/* Ce programme a été élaboré à partir du code de M. Olivier LEBRUN réutilisé en grande partie pour réaliser un encodeur OWL CM180 (Micro+)
/* ainsi qu'à partir du code (+ carte électronique à relier au compteur) de M. Pascal CARDON pour la partie téléinfo 
/* Onlinux a fourni des trames du OWL CM180 pour le reverse ingineering (il a développé lui même un code de décodage des trames, différent de mon code )
/* j'ai fait le reverse ingineering du OWL CM180 et les algo d'encodage pour le RFXCOM et RFLINK. Il n'est pas garantie que cet encodeur marche avec un autre recepteur que RFXCOM 
/* Je remercie les auteurs. Ci-dessous les liens vers leur site internet.
******** Snips *******

/* ***** version test du 01/05/2016 : rajout de la gestion de la tarifcation TEMPO (6 compteurs)  
// id bleu creuse/pleine 41/42 ; blanc creuse/pleine 43/44 ; rouge creuse/pleine 45/46  
/* ***** version du 13/06/2015 : adaptation pour un compteur teleinfo tarif de base (non HC et HP), 
// ****** adaptation pour un compteur qui ne retourne pas la puissance apparente PAPP (approximation IINST * 230)
// ****** exemple librairie LOWPOWER pour mettre en veille


/*=======================================================================================================================

ONLINUX :   Decode and parse the Oregon Scientific V3 radio data transmitted by OWL CM180 Energy sensor (433.92MHz)

http://blog.onlinux.fr

https://github.com/onlinux/OWL-CMR180
/*=======================================================================================================================


/*=======================================================================================================================
/*
 * connectingStuff, Oregon Scientific v2.1 Emitter
 * http://connectingstuff.net/blog/encodage-protocoles-oregon-scientific-sur-arduino/
 *
 * Copyright (C) 2013 olivier.lebrun@gmail.com
 
*/
//=======================================================================================================================


/*=======================================================================================================================
                                                        my_teleinfo
                                                 (c) 2012-2013 by  
                                                  Script name : my_teleinfo
                                 http://www.domotique-info.fr/2014/05/recuperer-teleinformation-arduino/

 Usage :                                                                                   
     + Arduino Teleinfo report program
     + This program receives data frames from the EDF counter teleinfo port, it parse it,
       validate each data group by verfying the checksum, stores it in local variables, 
       displays the actual counter, consumption ...
       Frame content is sent to a remote PHP server, thru Internet. The remote PHP
       server records the received data in a MySQL data base.                                                           
     + Runs on a Leonardo, RX on PIN 0
                                      
======================================================================================================================*/
// montage électronique conforme à http://www.domotique-info.fr/2014/05/recuperer-teleinformation-arduino/
// pin 8 de la carte arduino utilisé pour recevoir les informations téléinfo du compteur
// pin 3 utilisé pour l'emetteur 433 MHZ 


#include <SoftwareSerial.h>
#include <LowPower.h> //snips : télécharger la librairie LOwPower.h sur https://github.com/rocketscream/Low-Power/archive/master.zip et décompresser dans arduino\libraries


const byte TX_PIN = 3;  //emetteur 433 MHZ 
const unsigned long TIME = 488;
const unsigned long TWOTIME = TIME*2;

#define SEND_HIGH() digitalWrite(TX_PIN, HIGH)
#define SEND_LOW() digitalWrite(TX_PIN, LOW)
byte OregonMessageBuffer[13];  //  OWL180
//*********************************************************
SoftwareSerial* mySerial;
  char HHPHC;
  int ISOUSC;             // intensité souscrite  
  int IINST;              // intensité instantanée en A
  int IMAX;               // intensité maxi en A
  int PAPP;               // puissance apparente en VA
  unsigned long HCHC;  // compteur Heures Creuses en W
  unsigned long HCHP;  // compteur Heures Pleines en W
  String PTEC;            // Régime actuel : HPJB, HCJB, HPJW, HCJW, HPJR, HCJR
  String ADCO;            // adresse compteur
  String OPTARIF;         // option tarifaire
  String MOTDETAT;        // status word
  String pgmVersion;      // TeleInfo program version
  boolean ethernetIsOK;
  boolean teleInfoReceived;

unsigned long HPJB, HCJB, HPJW, HCJW, HPJR, HCJR; // compteurs tempo HC/HP X 3 jours-couleurs
String DEMAIN;
int IINST1,IINST2,IINST3;             
int IMAX1,IMAX2,IMAX3;     
int PMAX;
String PPOT;
  
  char chksum(char *buff, uint8_t len);
  boolean handleBuffer(char *bufferTeleinfo, int sequenceNumnber);
  char version[17] = "TeleInfo  V 1.00";
  
  unsigned long PAPP_arrondi;               // PAPP*497/500/16 arrondi
  unsigned long chksum_CM180;
  unsigned long long HCP; 
  
  //********** debug
 // char buffer[100];// à  virer ***************
//**********************************************************************

/**
 * \brief    Send logical "0" over RF
 * \details  azero bit be represented by an off-to-on transition
 * \         of the RF signal at the middle of a clock period.
 * \         Remenber, the Oregon v2.1 protocol add an inverted bit first 
 */
inline void sendZero(void)
{
  SEND_LOW();
  delayMicroseconds(TIME); 
  SEND_HIGH();
  delayMicroseconds(TIME);
}

/**
 * \brief    Send logical "1" over RF
 * \details  a one bit be represented by an on-to-off transition
 * \         of the RF signal at the middle of a clock period.
 * \         Remenber, the Oregon v2.1 protocol add an inverted bit first 
 */
inline void sendOne(void)
{
   SEND_HIGH();
   delayMicroseconds(TIME);
   SEND_LOW();
   delayMicroseconds(TIME);
}


/**
 * \brief    Send a buffer over RF
 * \param    data   Data to send
 * \param    size   size of data to send
 */
void sendData(byte *data, byte size)
{
  for(byte i = 0; i < size; ++i)
  {
  (bitRead(data[i], 0)) ? sendOne() : sendZero();
  (bitRead(data[i], 1)) ? sendOne() : sendZero();
  (bitRead(data[i], 2)) ? sendOne() : sendZero();
  (bitRead(data[i], 3)) ? sendOne() : sendZero();
  (bitRead(data[i], 4)) ? sendOne() : sendZero();
  (bitRead(data[i], 5)) ? sendOne() : sendZero();
  (bitRead(data[i], 6)) ? sendOne() : sendZero();
  (bitRead(data[i], 7)) ? sendOne() : sendZero();    
  }
}

/**
 * \brief    Send an Oregon message
 * \param    data   The Oregon message
 */
void sendOregon(byte *data, byte size)
{
    sendPreamble();
    sendData(data,size);   
    sendPostamble();
}

/**
 * \brief    Send preamble
 * \details  The preamble consists of 10 X "1" bits (minimum)
 */
inline void sendPreamble(void)
{
  for(byte i = 0; i < 10; ++i)   //OWL CM180
  {
    sendOne();
  }
}

/**
 * \brief    Send postamble
 */
inline void sendPostamble(void)
{

 for(byte i = 0; i <4 ; ++i)    //OWL CM180
  {
    sendZero() ;
  }
  SEND_LOW();
  delayMicroseconds(TIME);
}

//=================================================================================================================
// Basic constructor
//=================================================================================================================
void TeleInfo(String version)
{
  //  Serial.begin(1200,SERIAL_7E1);
  mySerial = new SoftwareSerial(8, 9); // RX, TX
  mySerial->begin(1200);
  pgmVersion = version;
  
  // variables initializations
  ADCO = "270622224349";
  OPTARIF = "----";
  ISOUSC = 0;
  HCHC = 0L;  // compteur Heures Creuses en W
  HCHP = 0L;  // compteur Heures Pleines en W
  PTEC = "----";    // Régime actuel : HPJB, HCJB, HPJW, HCJW, HPJR, HCJR (tempo)  ou HP.., HC.. (jour/nuit) oi TH.. base
  HHPHC = '-';
  IINST = 0;        // intensité instantanée en A
  IMAX = 0;         // intensité maxi en A
  PAPP = -1;         // puissance apparente en VA
  MOTDETAT = "------";
}

//=================================================================================================================
// Capture des trames de Teleinfo
//=================================================================================================================
boolean readTeleInfo(boolean ethernetIsConnected)
{
#define startFrame 0x02
#define endFrame 0x03
#define startLine 0x0A
#define endLine 0x0D
#define maxFrameLen 380

  int comptChar=0; // variable de comptage des caractères reçus 
  char charIn=0; // variable de mémorisation du caractère courant en réception

  char bufferTeleinfo[21] = "";
  int bufferLen = 0;
  int checkSum;

  ethernetIsOK = ethernetIsConnected;

  int sequenceNumnber= 0;    // number of information group
   
   // ************ snips : pour ceux qui n'ont pas de PAPP on réinitialise pour tester une valeur PAPP est reçu ***************
   PAPP = -1;         // puissance apparente en VA
    // ************ snips : fin ajout***********************************************************

  //--- wait for starting frame character 
  while (charIn != startFrame)
  { // "Start Text" STX (002 h) is the beginning of the frame
    if (mySerial->available())
     charIn = mySerial->read()& 0x7F; // Serial.read() vide buffer au fur et à mesure
  } // fin while (tant que) pas caractère 0x02
  //  while (charIn != endFrame and comptChar<=maxFrameLen)
  while (charIn != endFrame)
  { // tant que des octets sont disponibles en lecture : on lit les caractères
    // if (Serial.available())
    if (mySerial->available())
    {
      charIn = mySerial->read()& 0x7F;
      // incrémente le compteur de caractère reçus
      comptChar++;
      if (charIn == startLine)
        bufferLen = 0;
      bufferTeleinfo[bufferLen] = charIn;
      // on utilise une limite max pour éviter String trop long en cas erreur réception
      // ajoute le caractère reçu au String pour les N premiers caractères
      if (charIn == endLine)
      {
        checkSum = bufferTeleinfo[bufferLen -1];
        if (chksum(bufferTeleinfo, bufferLen) == checkSum)
        {// we clear the 1st character
          strncpy(&bufferTeleinfo[0], &bufferTeleinfo[1], bufferLen -3);
          bufferTeleinfo[bufferLen -3] =  0x00;
          sequenceNumnber++;
          Serial.println(bufferTeleinfo); // debug  
          Serial.println(sequenceNumnber); //debug
          if (!handleBuffer(bufferTeleinfo, sequenceNumnber))
          {
            Serial.println(F("Sequence error ..."));
       
            return false;
          }
        }
        else
        {
          Serial.println(F("Checksum error ..."));
    
          return false;
        }
      }
      else
        bufferLen++;
    }
    if (comptChar > maxFrameLen)
    {
      Serial.println(F("Overflow error ..."));

      return false;
    }
  }
  return true;
}

//=================================================================================================================
// Frame parsing
//=================================================================================================================
//void handleBuffer(char *bufferTeleinfo, uint8_t len)
boolean handleBuffer(char *bufferTeleinfo, int sequenceNumnber)
{
  // create a pointer to the first char after the space
  char* resultString = strchr(bufferTeleinfo,' ') + 1;
  boolean sequenceIsOK;

  switch(sequenceNumnber)
  {
  case 1:
    if (sequenceIsOK = bufferTeleinfo[0]=='A')
      ADCO = String(resultString);
    break;
  case 2:
    if (sequenceIsOK = bufferTeleinfo[0]=='O')
      OPTARIF = String(resultString);
    break;
  case 3:
    if (sequenceIsOK = bufferTeleinfo[1]=='S')
      ISOUSC = atol(resultString);
    break;
    
  default: 
      if (sequenceIsOK = (bufferTeleinfo[2]=='H' && bufferTeleinfo[3]=='C'))
          {
          HCHC = atol(resultString);
          break;
          }
 
      if (sequenceIsOK = (bufferTeleinfo[2]=='H' && bufferTeleinfo[3]=='P'))
          {
          HCHP = atol(resultString);
          break;
          }
    // *******snips : ajout pour gérer compteur de base *****************
    if (sequenceIsOK = (bufferTeleinfo[0]=='B' &&  bufferTeleinfo[1]=='A'))
          {
          HCHP = atol(resultString);
          break;
          }
   // ******* fin ajout pour gérer compteur de base *****************
 
   // *******snips : ajout pour gérer compteurs TEMPO *****************
   
      if (sequenceIsOK = bufferTeleinfo[0]=='D')
        {
        DEMAIN  = String(resultString);
        break;
        }
  

      if (sequenceIsOK = (bufferTeleinfo[4]=='P' && bufferTeleinfo[6]=='B'))
          {
          HPJB = atol(resultString);
          break;
          }
 
      if (sequenceIsOK = (bufferTeleinfo[4]=='C' && bufferTeleinfo[6]=='B'))
          {
          HCJB = atol(resultString);
          break;
          }
      if (sequenceIsOK = (bufferTeleinfo[4]=='P' && bufferTeleinfo[6]=='W'))
          {
          HPJW = atol(resultString);
          break;
          }
 
     if (sequenceIsOK = (bufferTeleinfo[4]=='C' && bufferTeleinfo[6]=='W'))
          {
          HCJW = atol(resultString);
          break;
          }    
          
          
     if (sequenceIsOK = (bufferTeleinfo[4]=='P' && bufferTeleinfo[6]=='R'))
          {
          HPJR= atol(resultString);
          break;
          }
 
     if (sequenceIsOK = (bufferTeleinfo[4]=='C' && bufferTeleinfo[6]=='R'))
          {
          HCJR = atol(resultString);
          break;
          }    
          
     if (sequenceIsOK = bufferTeleinfo[1]=='I')
          {
          if (bufferTeleinfo[5]='1') 
            {
              IINST1 =atol(resultString);
              break;
            }
            
          if (bufferTeleinfo[5]='2') 
            {
              IINST2 =atol(resultString);
              break;
            }
          
          if (bufferTeleinfo[5]='3') 
            {
              IINST3 =atol(resultString);
              break;
            }
         if (bufferTeleinfo[5]!='1' && bufferTeleinfo[5]!='2' && bufferTeleinfo[5]!='3')
            {
            IINST =atol(resultString);
            break;
            }
          }
  
    if (sequenceIsOK = bufferTeleinfo[1]=='M')
          {
          if (bufferTeleinfo[4]='1') 
            {
              IMAX1 =atol(resultString);
              break;
            }
            
           if (bufferTeleinfo[4]='2') 
              {
                IMAX2 =atol(resultString);
                break;
              }
          
          if (bufferTeleinfo[4]='3') 
            {
              IMAX3 =atol(resultString);
              break;
            }  
          
          if (bufferTeleinfo[4]!='1' && bufferTeleinfo[4]!='2' && bufferTeleinfo[4]!='3')
            {  
            IMAX =atol(resultString);
            break;
            }
          }
          
      if (sequenceIsOK = (bufferTeleinfo[0]=='P' && bufferTeleinfo[1]=='M'))
          {
          PMAX = atol(resultString);
          break;
          }    
      
      if (sequenceIsOK = (bufferTeleinfo[0]=='P' && bufferTeleinfo[1]=='P'))
          {
          PPOT = String(resultString);
          break;
          } 
          
   // ******* fin ajout pour gérer compteur TEMPO *****************   
   
    if (sequenceIsOK = bufferTeleinfo[1]=='T')
          {
          PTEC = String(resultString);
          break;
          }
  
    if (sequenceIsOK = (bufferTeleinfo[0]=='P' && bufferTeleinfo[1]=='A'))
          {
          PAPP =atol(resultString);
          break;
          }
    if (sequenceIsOK = bufferTeleinfo[1]=='H')
          {
          HHPHC = resultString[0];
          break;
          }
          
    if (sequenceIsOK = bufferTeleinfo[1]=='O')
          {
          MOTDETAT = String(resultString);
          break;
          }
  }

  
#ifdef debug
  if(!sequenceIsOK)
  {
    Serial.print(F("Out of sequence ..."));
    Serial.println(bufferTeleinfo);
  }
#endif
  return sequenceIsOK;
}

//=================================================================================================================
// Calculates teleinfo Checksum
//=================================================================================================================
char chksum(char *buff, uint8_t len)
{
  int i;
  char sum = 0;
  for (i=1; i<(len-2); i++) 
    sum = sum + buff[i];
  sum = (sum & 0x3F) + 0x20;
  return(sum);
}


//=================================================================================================================
// This function displays the TeleInfo Internal counters
// It's usefull for debug purpose
//=================================================================================================================
void displayTeleInfo()
{  
/*
ADCO 270622224349 B
 OPTARIF HC.. <
 ISOUSC 30 9
 HCHC 014460852 $
 HCHP 012506372 -
 PTEC HP..  
 IINST 002 Y
 IMAX 035 G
 PAPP 00520 (
 HHPHC C .
 MOTDETAT 000000 B
 */
  
Serial.print(F(" "));
  
  if (PTEC.substring(2,3)=="J")   // exemple PTEC = HCJB etc   ou HP/HC ou TH 
  {
  Serial.println();
  Serial.print(F("ADCO "));
  Serial.println(ADCO);
  Serial.print(F("OPTARIF "));
  Serial.println(OPTARIF);
  Serial.print(F("ISOUSC "));
  Serial.println(ISOUSC);

      Serial.print(F("HPJB "));
      Serial.println(HPJB);
      Serial.print(F("HCJB "));
      Serial.println(HCJB);
      Serial.print(F("HPJW "));
      Serial.println(HPJW);
      Serial.print(F("HCJW "));
      Serial.println(HCJW);
      Serial.print(F("HPJR "));
      Serial.println(HPJR);
      Serial.print(F("HCJR "));
      Serial.println(HCJR);
  /*
      Serial.print(F("HCHC "));
      Serial.println(HCHC);
      Serial.print(F("HCHP "));
      Serial.println(HCHP);
  */ 
  Serial.print(F("PTEC "));
  Serial.println(PTEC);
 
  Serial.print(F("DEMAIN "));
  Serial.println(DEMAIN);
  
  Serial.print(F("IINST1 "));
  Serial.println(IINST1);
  Serial.print(F("IINST2 "));
  Serial.println(IINST2);
  Serial.print(F("IINST3 "));
  Serial.println(IINST3);
  
  Serial.print(F("IMAX1 "));
  Serial.println(IMAX1);
  Serial.print(F("IMAX2 "));
  Serial.println(IMAX2);
  Serial.print(F("IMAX3 "));
  Serial.println(IMAX3);
  
  Serial.print(F("PMAX "));
  Serial.println(PMAX);  
  
  /*
       Serial.print(F("IINST "));
       Serial.println(IINST);
       Serial.print(F("IMAX "));
       Serial.println(IMAX);
   */
  Serial.print(F("PAPP "));
  Serial.println(PAPP);
  Serial.print(F("HHPHC "));
  Serial.println(HHPHC);
  Serial.print(F("MOTDETAT "));
  Serial.println(MOTDETAT);   
  
  Serial.print(F("PPOT "));
  Serial.println(PPOT); 
  }
  else
  {
  Serial.println();
  Serial.print(F("ADCO "));
  Serial.println(ADCO);
  Serial.print(F("OPTARIF "));
  Serial.println(OPTARIF);
  Serial.print(F("ISOUSC "));
  Serial.println(ISOUSC);
  Serial.print(F("HCHC "));
  Serial.println(HCHC);
  Serial.print(F("HCHP "));
  Serial.println(HCHP);
  Serial.print(F("PTEC "));
  Serial.println(PTEC);
  Serial.print(F("IINST "));
  Serial.println(IINST);
  Serial.print(F("IMAX "));
  Serial.println(IMAX);
  Serial.print(F("PAPP "));
  Serial.println(PAPP);
  Serial.print(F("HHPHC "));
  Serial.println(HHPHC);
  Serial.print(F("MOTDETAT "));
  Serial.println(MOTDETAT);    
  }
}



void encodeur_OWL_CM180()
{

if (PTEC.substring(0,4)=="HC..")
{
  HCP=(HCHC*223666LL)/1000LL;
}
if (PTEC.substring(0,4)=="HP..")
{
  HCP=(HCHP*223666LL)/1000LL;
}

// ********* snips : si tarif de base ******************* 
if (PTEC.substring(1,2)=="H")
{
  HCP=(HCHP*223666LL)/1000LL;
}
// ********* snips : fin tarif de base *******************

//************ snips : si  tempo ************************* 
if (PTEC.substring(0,4)=="HCJB")
{
  HCP=(HCJB*223666LL)/1000LL;
}
if (PTEC.substring(0,4)=="HPJB")
{
  HCP=(HPJB*223666LL)/1000LL;
}
if (PTEC.substring(0,4)=="HCJW")
{
  HCP=(HCJW*223666LL)/1000LL;
}
if (PTEC.substring(0,4)=="HPJW")
{
  HCP=(HPJW*223666LL)/1000LL;
}
if (PTEC.substring(0,4)=="HCJR")
{
  HCP=(HCJR*223666LL)/1000LL;
}
if (PTEC.substring(0,4)=="HPJR")
{
  HCP=(HPJR*223666LL)/1000LL;
}

//************ snips : fin  tempo *************************

// ********* snips : si pas de PAPP reçu du compteur on approxime PAPP ******************* 
if (PAPP==-1)
{
  PAPP=IINST*230LL;
}
// ********* snips : fin pas de PAPP  *******************


OregonMessageBuffer[0] =0x62; // imposé
OregonMessageBuffer[1] =0x80; // GH   G= non décodé par RFXCOLM,  H = Count

// identifiant compteur
OregonMessageBuffer[2] =0x3C; // IJ  ID compteur : "L IJ 2" soit (L & 1110 )*16*16*16+I*16*16+J*16+2

//OregonMessageBuffer[3] =0xE1; // KL  K sert pour puissance instantanée, L sert pour identifiant compteur 
PAPP_arrondi=long(long(PAPP)*497/500/16);

// améliore un peu la précision de la puissance apparente encodée (le CM180 transmet la PAPP * 497/500/16)
if ((float(PAPP)*497/500/16-PAPP_arrondi)>0.5)
{
++PAPP_arrondi;
}

OregonMessageBuffer[3]=(PAPP_arrondi&0x0F)<<4;

//OregonMessageBuffer[4] =0x00; // MN  puissance instantée = (P MN K)*16 soit : (P*16*16*16 + M*16*16 +N*16+K)*16*500/497. attention RFXCOM corrige cette valeur en multipliant par 16 puis 500/497.
OregonMessageBuffer[4]=(PAPP_arrondi>>4)&0xFF;

//OregonMessageBuffer[5] =0xCD; // OP  Total conso :  YZ WX UV ST QR O : Y*16^10 + Z*16^9..R*16 + O
OregonMessageBuffer[5] =((PAPP_arrondi>>12)&0X0F)+((HCP&0x0F)<<4);

//OregonMessageBuffer[6] =0x97; // QR sert total conso
OregonMessageBuffer[6] =(HCP>>4)&0xFF;

//OregonMessageBuffer[7] =0xCE; // ST sert total conso
OregonMessageBuffer[7] =(HCP>>12)&0xFF; // ST sert total conso

//OregonMessageBuffer[8] =0x12; // UV sert total conso
OregonMessageBuffer[8] =(HCP>>20)&0xFF; // UV sert total conso

//OregonMessageBuffer[9] =0x00; // WX sert total conso
OregonMessageBuffer[9] =(HCP>>28)&0xFF; 

//OregonMessageBuffer[10] =0x00; //YZ sert total conso
OregonMessageBuffer[10] =(HCP>>36)&0xFF; 


  chksum_CM180= 0;
  for (byte i=0; i<11; i++) 
  {
    chksum_CM180 += long(OregonMessageBuffer[i]&0x0F) + long(OregonMessageBuffer[i]>>4) ;
  }
  
chksum_CM180 -=2; // = =b*16^2 + d*16+ a ou [b d a]

//OregonMessageBuffer[11] =0xD0; //ab sert CHECKSUM  somme(nibbles ci-dessuus)=b*16^2 + d*16+ a + 2
OregonMessageBuffer[11] =((chksum_CM180&0x0F)<<4) + ((chksum_CM180>>8)&0x0F);

//OregonMessageBuffer[12] =0xF6; //cd  d sert checksum, a non décodé par RFXCOM
OregonMessageBuffer[12] =(int(chksum_CM180>>4)&0x0F);  //C = 0 mais inutilisé

}

//************************************************************************************

void setup() {
  
  Serial.begin(115200);   // pour la console, enlever les barres de commentaires ci dessous pour displayTeleInfo()
  TeleInfo(version); 
}

void loop() {
teleInfoReceived=readTeleInfo(true);
if (teleInfoReceived)
  {
  encodeur_OWL_CM180();
  mySerial->end(); //NECESSAIRE !! arrête les interruptions de softwareserial (lecture du port téléinfo) pour émission des trames OWL 
  sendOregon(OregonMessageBuffer, sizeof(OregonMessageBuffer));    // Send the Message over RF
  //**********snips : exemple de mise en veille - activer les lignes ci-dessous pour faire dormir l'arduino pendant 8 secondes ***************************
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  //**********snips : fin   exemple de mise en veille *****************************************************************************************
  mySerial->begin(1200);  //NECESSAIRE !! relance les interuptions pour la lecture du port téléinfo
  displayTeleInfo();  // console pour voir les trames téléinfo.. désactiver cette ligne pour économiser de l'énergie
  }

}
