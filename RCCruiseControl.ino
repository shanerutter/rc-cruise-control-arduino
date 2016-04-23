// TODO: No signal disable cruise
// TODO: Determine center off-set automatically
// TODO: Determine forward / reverse directions
// TODO: Test on other recievers / setups
// TODO: Trigger programming mode (AUX + Break) on startup within 500ms of starting

/*
 * Pin assignemnt
 * Will require hardware changes if these are changed
 */
const short recieverThrottlePin = 2; // Reciever input (Digital input)
const short recieverThrottleInterruptPin = 0; // Reciever input (Interrupt), same pin as recieverPin but the interrupt ID
const short recieverAuxPin = 3; // Reciever input (Digital input)
const short recieverAuxInterruptPin = 1; // Reciever input (Interrupt), same pin as recieverPin but the interrupt ID
const short recieverOutPin = 7; // Pin used for killing engine
const short recieverOutPinBit = 7; // Related to "PORTD" bit mapping (fast updating via bit mapping)
const short statusLEDPin = 13;
const short statusLEDPinBit = 5;

/*
 * Variables
 */
volatile short recieveThrottleShared = 0; // Recieve position data (shared between interrupt and main loop)
volatile short recieveAuxShared = 0; // Recieve position data (shared between interrupt and main loop)
bool cruiseControl = false;
short savedCruiseSpeed = 0;
unsigned long lastMillis = 0;
unsigned short operationMode = 0;
unsigned short configureStage = 0;

// Config variables for storing data during config mode
// These are parsed and saved into the EEPROM
short cReverseStartingPos, cReverseToCenter, cCenterToThrottle, cThrottleToCenter;


bool reversedRecieverInput = false; // Do we have reversed reciever input? E.G 1000 is full throttle instead of 2000
short reverseCenterOffset = 20;
short throttleCenterOffset = 20;




/*
 * Perform start up operations
 */
void setup() {
    pinMode(recieverOutPin, OUTPUT); // Pin used for killing engine (optocoupler)
  
    pinMode(recieverThrottlePin, INPUT); // Make are reciever pin an input
    attachInterrupt(recieverThrottleInterruptPin, calcRecieverThrottlePin, CHANGE); // Trigger interrupt and call calcRecieverPin runction each time a change is detected

    pinMode(recieverAuxPin, INPUT); // Make are reciever pin an input
    attachInterrupt(recieverAuxInterruptPin, calcRecieverAuxPin, CHANGE); // Trigger interrupt and call calcRecieverPin runction each time a change is detected

    pinMode(statusLEDPin, OUTPUT);
    PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)

    //Serial.begin(9600);
}

/*
 * Main program loop
 */
void loop() {
  // Lets store the shared data into temp local variables, this allows us to quickly disable interrupts and enable again as quickly as possible
  static short recieveThrottle;
  static short recieveAux;

  // throttlePosition after it has been processed
  static short throttlePosition = 0;

  // Do we have a reciever update?
  if(recieveThrottleShared != 0){
    // Disable interrupts
    noInterrupts();

    // Map shared data to local variables
    recieveThrottle = recieveThrottleShared;
    recieveThrottleShared = 0;

    // Enable interrupts, by doing this shared to local assignment it allows us to continue processing, but also allows the interrupts to continue firing and collecting data
    // After this line we can now perform are slower processing of the reciever data without worry of holding back any interrupts
    interrupts();
  }

  // Do we have a reciever update?
  if(recieveAuxShared != 0){
    // Disable interrupts
    noInterrupts();

    // Map shared data to local variables
    recieveAux = recieveAuxShared;
    recieveAuxShared = 0;

    // Enable interrupts, by doing this shared to local assignment it allows us to continue processing, but also allows the interrupts to continue firing and collecting data
    // After this line we can now perform are slower processing of the reciever data without worry of holding back any interrupts
    interrupts();
  }



  

  // Are we entering configure mode?
  if(operationMode == 0 && millis() < 500){
    if(recieveAux > 1700 && (recieveThrottle < 1300 || recieveThrottle > 1700)){
      operationMode = 1;
    }
  // Go to normal operating mode after 500ms, this gives time to enter config mode and for reciever to start before passing data to ESC
  }else if(operationMode == 0 && millis() > 500){
    operationMode = 2;
  }

  // We are in configure mode
  // Process each step so we can calc various offset data and throttle direction
  if(operationMode == 1){
    //cReverseStartingPos is set when operatingMode = 1 is set

    if(recieveAux > 1700){
      PORTB |= 1<<statusLEDPinBit; // Set bit 5 high (Pin 13)
    }else{
      PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)
    }

    // Stage 0 - Config start indicator
    if(configureStage == 0){
      // Indicate config started
      PORTB |= 1<<statusLEDPinBit; // Set bit 5 high (Pin 13)
      delay(500);
      PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)
      delay(500);
      PORTB |= 1<<statusLEDPinBit; // Set bit 5 high (Pin 13)
      delay(500);
      PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)
      delay(500);
      PORTB |= 1<<statusLEDPinBit; // Set bit 5 high (Pin 13)
        
      cReverseStartingPos = recieveThrottle;
      configureStage = 1;
    }
    
    // Step 1 - Reverse to Center then toggle Aux
    if(configureStage == 1){
      if(recieveAux < 1300){
        cReverseToCenter = recieveThrottle;
        configureStage = 2;
      }
    }

    // Step 2 - Center to Full Throttle then toggle Aux
    if(configureStage == 2){
      if(recieveAux > 1700){
        cCenterToThrottle = recieveThrottle;
        configureStage = 3;
      }
    }

    // Step 3 - Full Throttle to Center then toggle Aux
    if(configureStage == 3){
      if(recieveAux < 1300){
        cThrottleToCenter = recieveThrottle;
        configureStage = 4;
      }
    }

    // Step 4 - Process config data and save
    if(configureStage == 4){
      // Calculate config settings
      reversedRecieverInput = (cReverseStartingPos > 1700) ? true : false;
      reverseCenterOffset = abs(cReverseToCenter - 1500) * 2;
      throttleCenterOffset = abs(cThrottleToCenter - 1500) * 2;

      // Save config settings to EEPROM


      // Indicate config completed
      PORTB |= 1<<statusLEDPinBit; // Set bit 5 high (Pin 13)
      delay(500);
      PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)
      delay(500);
      PORTB |= 1<<statusLEDPinBit; // Set bit 5 high (Pin 13)
      delay(500);
      PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)
      delay(500);
      PORTB |= 1<<statusLEDPinBit; // Set bit 5 high (Pin 13)
      delay(500);
      PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)
      delay(500);
      PORTB |= 1<<statusLEDPinBit; // Set bit 5 high (Pin 13)
      delay(500);
      PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)

      // Return values to defaults and set operationMode to normal functionality
      configureStage = 0;
      operationMode = 2;

      return;
    }
    
  }
  

  // Are we in normal operating mode?
  if(operationMode == 2){
    throttlePosition = recieveThrottle;

    // Set a safe area around center, this stops fluxuations triggering forward / reverse by accident
    if(reversedRecieverInput){
      if(throttlePosition >= (1500 - throttleCenterOffset) && throttlePosition <= (1500 + reverseCenterOffset)) throttlePosition = 1500;
    }else{
      if(throttlePosition >= (1500 - reverseCenterOffset) && throttlePosition <= (1500 + throttleCenterOffset)) throttlePosition = 1500;
    }

    // If Aux has had a change lets toggle cruise control
    if(recieveAux){
      if(recieveAux > 1700){
        if(cruiseControl == false){
          cruiseControl = true;
          savedCruiseSpeed = throttlePosition;
          PORTB |= 1<<statusLEDPinBit; // Set bit 5 high (Pin 13)
        }
      }else{
        if(cruiseControl == true){
          cruiseControl = false;
          savedCruiseSpeed = 0;
          PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)
        }
      }
    }
  
    // If reverse input while cruise enabled set saved cruse speed to 0
    // This will lave cruse enabled but with a saved speed of 0 which is technically disabled
    // But it means cruse will not reengage until toggled
    if(cruiseControl && savedCruiseSpeed != 0){
      if(reversedRecieverInput){
        if(throttlePosition > 1500) savedCruiseSpeed = 0;
      }else{
        if(throttlePosition < 1500) savedCruiseSpeed = 0;
      }

      if(savedCruiseSpeed == 0) PORTB &= ~(1<<statusLEDPinBit); // Set bit 5 low (Pin 13)
    }

    // IMPORTANT - This is where the throttlePosition may be overwritten with the cruise speed, any code which needs to current live throttlePosision needs to be above this line
    // If cruise is enable used savedCruiseSpeed unless user input throttle is greater than savedCruiseSpeed
    // If cruise is disabled just use the live throttlePosition
    if(!cruiseControl){
      throttlePosition = throttlePosition;
    }else{
      if(reversedRecieverInput){
        throttlePosition = (savedCruiseSpeed == 0 || throttlePosition < savedCruiseSpeed) ? throttlePosition : savedCruiseSpeed;
      }else{
        throttlePosition = (savedCruiseSpeed == 0 || throttlePosition > savedCruiseSpeed) ? throttlePosition : savedCruiseSpeed;
      }
    }
  
    //Serial.print(cruiseControl);
    //Serial.print(" - ");
    //Serial.print(savedCruiseSpeed);
    //Serial.print(" - ");
    //Serial.println(throttlePosition);
  
    // Output servo throttle every 20ms
    // http://webboggles.com/pwm-servo-control-with-attiny85/
    // Send a pulse every 20 ms that is in the range between 1000 and 2000 nanoseconds
    if(millis() > lastMillis + 20){
      PORTD |= 1<<recieverOutPinBit; // Set bit 7 high (Pin 7)
      unsigned long start = micros();
      while (micros() < start + throttlePosition){} // Wait duration of throttle position
      PORTD &= ~(1<<recieverOutPinBit); // Set bit 7 low (Pin 7)
      lastMillis = millis(); // reset timer
    }
  }

  


}

/*
 * Calc reciever throttle pin input
 */
void calcRecieverThrottlePin(){
  static uint32_t ulStart;

  if(digitalRead(recieverThrottlePin))
  {
    ulStart = micros();
  }
  else
  {
    recieveThrottleShared = (uint32_t)(micros() - ulStart);
  }
}

/*
 * Calc reciever aux pin input
 */
void calcRecieverAuxPin(){
  static uint32_t ulStart;

  if(digitalRead(recieverAuxPin))
  {
    ulStart = micros();
  }
  else
  {
    recieveAuxShared = (uint32_t)(micros() - ulStart);
  }
}
