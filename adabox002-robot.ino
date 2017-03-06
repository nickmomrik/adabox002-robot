#include <string.h>
#include <Arduino.h>
#include <SPI.h>
#if not defined ( _VARIANT_ARDUINO_DUE_X_ )
  #include <SoftwareSerial.h>
#endif

#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_SPI.h"
#include "Adafruit_BluefruitLE_UART.h"

#include "BluefruitConfig.h"

#include <Wire.h>
#include <Adafruit_MotorShield.h>

// Create the motor shield object with the default I2C address
Adafruit_MotorShield AFMS = Adafruit_MotorShield(); 

// And connect 2 DC motors to port M3 & M4 !
Adafruit_DCMotor *L_MOTOR = AFMS.getMotor( 3 );
Adafruit_DCMotor *R_MOTOR = AFMS.getMotor( 4 );

// Name of Robot
String BROADCAST_NAME = "AdaBox002 Robot";
String BROADCAST_CMD = String( "AT+GAPDEVNAME=" + BROADCAST_NAME );

Adafruit_BluefruitLE_SPI ble( BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST );

// Tone for reverse
#define BEEP 1911

// Pins
#define SPEAKER       A1
#define LEFT_BLINKER  9
#define RIGHT_BLINKER 10
#define GO_LIGHT      11
#define STOP_LIGHT    12

// States
#define GO_REVERSE -1
#define GO_NONE    0
#define GO_FORWARD 1
#define TURN_LEFT  -1
#define TURN_NONE  0
#define TURN_RIGHT 1

#define MAX_SPEED    255
#define SPEED_CHANGE 5

// How often in milliseconds updates happen
#define UPDATE_ROBOT_TIME 5
#define UPDATE_BLINK_TIME 150

// A small helper
void error( const __FlashStringHelper*err ) {
  Serial.println( err );
  while ( 1 );
}

// function prototypes over in packetparser.cpp
uint8_t readPacket( Adafruit_BLE *ble, uint16_t timeout );
float parsefloat( uint8_t *buffer );
void printHex( const uint8_t * data, const uint32_t numBytes );

// the packet buffer
extern uint8_t packetbuffer[];

char buf[60];

// Statuses
int goStatus    = GO_NONE;
int turnStatus  = TURN_NONE;
int speedStatus = 1; // 1 = 25%, 2 = 50%, 3 = 75%, 4 = 100% of the max speed

int currentButtonPress = 0;

int currentSpeedLeft  = 0;
int currentSpeedRight = 0;

int currentGoLeft  = GO_NONE;
int currentGoRight = GO_NONE;

int currentLeftBlinker  = LOW;
int currentRightBlinker = LOW;
int currentStopLight    = LOW;

unsigned long previousUpdate      = 0;
unsigned long previousBlinkUpdate = 0;


/**************************************************************************/
/*!
    @brief  Sets up the HW an the BLE module (this function is called
            automatically on startup)
*/
/**************************************************************************/
void setup( void ) {
  Serial.begin( 9600 );

  AFMS.begin();  // create with the default frequency 1.6KHz

  // turn on motors
  L_MOTOR->setSpeed( 0 );
  R_MOTOR->setSpeed( 0 );
  L_MOTOR->run( RELEASE );
  R_MOTOR->run( RELEASE );
    
  Serial.begin( 115200 );
  Serial.println( F( "Adafruit Bluefruit Robot Controller Example" ) );
  Serial.println( F( "-----------------------------------------" ) );
  
  pinMode( SPEAKER, OUTPUT );
  
  pinMode( RIGHT_BLINKER, OUTPUT );
  pinMode( LEFT_BLINKER, OUTPUT );
  pinMode( GO_LIGHT, OUTPUT );
  pinMode( STOP_LIGHT, OUTPUT );

  digitalWrite( GO_LIGHT, LOW );
  digitalWrite( STOP_LIGHT, LOW );
  digitalWrite( LEFT_BLINKER, LOW );
  digitalWrite( RIGHT_BLINKER, LOW );

  /* Initialize the module */
  BLEsetup();
}

void loop( void ) {
  // read new packet data
  uint8_t len = readPacket( &ble, BLE_READPACKET_TIMEOUT );

  readController();

  updateRobot();
}

void updateRobot() {
  unsigned long currentTime = millis();

/*
  Serial.println( "***************" );
  Serial.print( "updateRobot " );
  Serial.println( millis() );
  Serial.print( "LeftSpeed  " );
  Serial.print( currentSpeedLeft );
  Serial.print( " LeftGo  " );
  Serial.println( currentGoLeft );
  Serial.print( "RightSpeed " );
  Serial.print( currentSpeedRight );
  Serial.print( " RightGo " );
  Serial.println( currentGoRight );
  Serial.print( "goStatus " );
  Serial.print( goStatus );
  Serial.print( " turnStatus " );
  Serial.println( turnStatus );
*/

  if ( GO_NONE == currentGoLeft && GO_NONE == currentGoRight ) {
    digitalWrite( STOP_LIGHT, HIGH );
    digitalWrite( GO_LIGHT, LOW );
  }
  
  if ( 0 == currentSpeedLeft && 0 == currentSpeedRight ) {
    if ( GO_REVERSE == goStatus ) {
      digitalWrite( GO_LIGHT, LOW );
      digitalWrite( STOP_LIGHT, HIGH );
      L_MOTOR->run( BACKWARD );
      R_MOTOR->run( BACKWARD );
      currentGoLeft  = GO_REVERSE;
      currentGoRight = GO_REVERSE;
    } else {
      digitalWrite( GO_LIGHT, HIGH );
      digitalWrite( STOP_LIGHT, LOW );
      L_MOTOR->run( FORWARD );
      R_MOTOR->run( FORWARD );
      currentGoLeft  = GO_FORWARD;
      currentGoRight = GO_FORWARD;
    }
  }

  int desiredSpeedLeft  = getSpeed();
  int desiredSpeedRight = getSpeed();

  if ( ( currentTime - previousUpdate ) >= UPDATE_ROBOT_TIME ) {
    if ( GO_FORWARD == goStatus ) {
      if ( GO_REVERSE == currentGoLeft || GO_REVERSE == currentGoRight ) {
        desiredSpeedLeft = 0;
        desiredSpeedRight = 0;
      } else {
        if ( TURN_RIGHT == turnStatus ) {
          desiredSpeedRight *= 0.5;
        } else if ( TURN_LEFT == turnStatus ) {
          desiredSpeedLeft *= 0.5;
        }
      }
    } else if ( GO_REVERSE == goStatus ) {
      if ( GO_FORWARD == currentGoLeft || GO_FORWARD == currentGoRight ) {
        desiredSpeedLeft = 0;
        desiredSpeedRight = 0;
      } else {
        if ( TURN_RIGHT == turnStatus ) {
          desiredSpeedLeft *= 0.5;
        } else if ( TURN_LEFT == turnStatus ) {
          desiredSpeedRight *= 0.5;
        }
      }
    } else {
      if ( TURN_RIGHT == turnStatus ) {
        desiredSpeedLeft = 0;
      } else if ( TURN_LEFT == turnStatus ) {
        desiredSpeedRight = 0;
      } else {
        desiredSpeedLeft = 0;
        desiredSpeedRight = 0;
      }
    }

/*
    Serial.print( "Desired LeftSpeed  " );
    Serial.println( desiredSpeedLeft );
    Serial.print( "Desired RightSpeed  " );
    Serial.println( desiredSpeedRight );
*/

    while ( desiredSpeedLeft != currentSpeedLeft || desiredSpeedRight != currentSpeedRight ) {
      if ( desiredSpeedLeft != currentSpeedLeft ) {
        int newSpeedLeft;
  
        if ( desiredSpeedLeft < currentSpeedLeft ) {
          newSpeedLeft = max( currentSpeedLeft - SPEED_CHANGE, desiredSpeedLeft );
        } else {
          newSpeedLeft = min( currentSpeedLeft + SPEED_CHANGE, desiredSpeedLeft );
        }
  
        L_MOTOR->setSpeed( newSpeedLeft );
        currentSpeedLeft = newSpeedLeft;
  
        if ( 0 == currentSpeedLeft ) {
          L_MOTOR->run( RELEASE );
          currentGoLeft = GO_NONE;
        }
      }
  
      if ( desiredSpeedRight != currentSpeedRight ) {
        int newSpeedRight;
  
        if ( desiredSpeedRight < currentSpeedRight ) {
          newSpeedRight = max( currentSpeedRight - SPEED_CHANGE, desiredSpeedRight );
        } else {
          newSpeedRight = min( currentSpeedRight + SPEED_CHANGE, desiredSpeedRight );
        }
  
        R_MOTOR->setSpeed( newSpeedRight );
        currentSpeedRight = newSpeedRight;
  
        if ( 0 == currentSpeedRight ) {
          R_MOTOR->run( RELEASE );
          currentGoRight = GO_NONE;
        }
      }

      delay( 10 );
    }

    previousUpdate = currentTime;
  }

  // turnStatus may not be correct asap due to potential time to slow down and switch directions
  // maybe a preparing direction flag?
  if ( ( currentTime - previousBlinkUpdate ) >= UPDATE_BLINK_TIME ) {
    if ( TURN_RIGHT == turnStatus ) {
      maybeTurnOffBlink( LEFT_BLINKER );

      currentRightBlinker = ! currentRightBlinker;
      digitalWrite( RIGHT_BLINKER, currentRightBlinker );
    } else if ( TURN_LEFT == turnStatus ) {
      maybeTurnOffBlink( RIGHT_BLINKER );

      currentLeftBlinker = ! currentLeftBlinker;
      digitalWrite( LEFT_BLINKER, currentLeftBlinker );
    } else {
      maybeTurnOffBlink( LEFT_BLINKER );
      maybeTurnOffBlink( RIGHT_BLINKER );
    }

    if ( GO_REVERSE == goStatus && ( GO_REVERSE == currentGoLeft || GO_REVERSE == currentGoRight ) ) {
      currentStopLight = ! currentStopLight;
      digitalWrite( STOP_LIGHT, currentStopLight );
      //digitalWrite( SPEAKER, currentStopLight );
    } else {
      maybeTurnOffBlink( STOP_LIGHT );
      //maybeTurnOffBlink( SPEAKER );
    }

    previousBlinkUpdate = currentTime;
  }
}

void readController() {
  if ( packetbuffer[1] == 'B' ) {
    uint8_t buttNum = packetbuffer[2] - '0';
    boolean pressed = packetbuffer[3] - '0';

    if ( pressed && buttNum != currentButtonPress ) {
      //Serial.println( buttNum );

      currentButtonPress = buttNum;

      switch ( buttNum ) {
        case 1:
        case 2:
        case 3:
        case 4:
          speedStatus = buttNum;
          break;

        case 5:
          if ( GO_FORWARD == goStatus ) {
            goStatus = GO_NONE;
          } else {
            goStatus = GO_FORWARD;
          }

          break;

        case 6:
          if ( GO_REVERSE == goStatus ) {
            goStatus = GO_NONE;
          } else {
            goStatus = GO_REVERSE;
          }

          break;

        case 7:
          if ( TURN_LEFT == turnStatus ) {
            turnStatus = TURN_NONE;
          } else {
            turnStatus = TURN_LEFT;
          }

          break;

        case 8:
          if ( TURN_RIGHT == turnStatus ) {
            turnStatus = TURN_NONE;
          } else {
            turnStatus = TURN_RIGHT;
          }

          break;
      }
    } else {
      currentButtonPress = 0;
    }
  }
}

void BLEsetup() {
  Serial.print( F( "Initialising the Bluefruit LE module: " ) );

  if ( ! ble.begin( VERBOSE_MODE ) ) {
    error( F( "Couldn't find Bluefruit, make sure it's in CoMmanD mode & check wiring?" ) );
  }
  Serial.println( F( "OK!" ) );

  /* Perform a factory reset to make sure everything is in a known state */
  Serial.println( F( "Performing a factory reset: " ) );
  if ( ! ble.factoryReset() ) {
    error( F( "Couldn't factory reset" ) );
  }

  //Convert the name change command to a char array
  BROADCAST_CMD.toCharArray( buf, 60 );

  //Change the broadcast device name here!
  if ( ble.sendCommandCheckOK( buf ) ) {
    Serial.println( "name changed" );
  }
  delay( 250 );

  //reset to take effect
  if ( ble.sendCommandCheckOK( "ATZ" ) ) {
    Serial.println( "resetting" );
  }
  delay( 250 );

  //Confirm name change
  ble.sendCommandCheckOK( "AT+GAPDEVNAME" );

  /* Disable command echo from Bluefruit */
  ble.echo( false );

  Serial.println( "Requesting Bluefruit info:" );
  /* Print Bluefruit information */
  ble.info();

  Serial.println( F( "Please use Adafruit Bluefruit LE app to connect in Controller mode" ) );
  Serial.println( F( "Then activate/use the sensors, color picker, game controller, etc!" ) );
  Serial.println();

  ble.verbose( false );  // debug info is a little annoying after this point!

  /* Wait for connection */
  while ( ! ble.isConnected() ) {
    delay( 500 );
  }

  Serial.println( F( "*****************" ) );

  // Set Bluefruit to DATA mode
  Serial.println( F( "Switching to DATA mode!" ) );
  ble.setMode( BLUEFRUIT_MODE_DATA );

  Serial.println( F( "*****************" ) );
}

int getSpeed() {
  return round( speedStatus * 0.25 * ( MAX_SPEED + 1 ) ) - 1;
}

void maybeTurnOffBlink( int light ) {
  bool turnOff = false;

  if ( LEFT_BLINKER == light ) {
    if ( HIGH == currentLeftBlinker ) {
      currentLeftBlinker = LOW;
      turnOff = true;
    }
  } else if ( RIGHT_BLINKER == light ) {
    if ( HIGH == currentRightBlinker ) {
      currentRightBlinker = LOW;
      turnOff = true;
    }
  } else if ( STOP_LIGHT == light ) {
    if ( HIGH == currentStopLight ) {
      currentStopLight = LOW;
      turnOff = true;
    }
  }

  if ( turnOff ) {
    digitalWrite( light, LOW );
  }
}

