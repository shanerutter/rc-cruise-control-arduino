// TODO: No signal disable cruise
// TODO: Test on other recievers / setups

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

/*
 * Variables
 */
volatile uint32_t recieveThrottleShared = 0; // Recieve position data (shared between interrupt and main loop)
volatile uint32_t recieveAuxShared = 0; // Recieve position data (shared between interrupt and main loop)
bool cruiseControl = false;
short savedCruiseSpeed = 0;
unsigned long lastMillis = 0;


/*
 * Perform start up operations
 */
void setup() {
    pinMode(recieverOutPin, OUTPUT); // Pin used for killing engine (optocoupler)
  
    pinMode(recieverThrottlePin, INPUT); // Make are reciever pin an input
    attachInterrupt(recieverThrottleInterruptPin, calcRecieverThrottlePin, CHANGE); // Trigger interrupt and call calcRecieverPin runction each time a change is detected

    pinMode(recieverAuxPin, INPUT); // Make are reciever pin an input
    attachInterrupt(recieverAuxInterruptPin, calcRecieverAuxPin, CHANGE); // Trigger interrupt and call calcRecieverPin runction each time a change is detected

    //Serial.begin(9600);
}

/*
 * Main program loop
 */
void loop() {
  // Lets store the shared data into temp local variables, this allows us to quickly disable interrupts and enable again as quickly as possible
  static uint32_t recieveThrottle;
  static uint32_t recieveAux;

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

  // Subtract a small amount to bring the input more to the center value
  // Override center point with offsets for better accuracy
  throttlePosition = recieveThrottle - 20;
  if(throttlePosition >= 1460 && throttlePosition <= 1520) throttlePosition = 1500;

  // If Aux has had a change lets toggle cruise control
  if(recieveThrottle || recieveAux){
    if(recieveAux > 1800){
      if(cruiseControl == false){
        cruiseControl = true;
        savedCruiseSpeed = throttlePosition;
      }
    }else{
      if(cruiseControl == true){
        cruiseControl = false;
        savedCruiseSpeed = 0;
      }
    }
  }

  // If reverse input while cruise enabled set saved cruse speed to 0
  // This will lave cruse enabled but with a saved speed of 0 which is technically disabled
  // But it means cruse will not reengage until toggled
  if(throttlePosition < 1500 && cruiseControl) savedCruiseSpeed = 0;

  // IMPORTANT - This is where the throttlePosition may be overwritten with the cruise speed, any code which needs to current live throttlePosision needs to be above this line
  // If cruise is enable used savedCruiseSpeed unless user input throttle is greater than savedCruiseSpeed
  // If cruise is disabled just use the live throttlePosition
  if(!cruiseControl){
    throttlePosition = throttlePosition;
  }else{
    throttlePosition = (throttlePosition > savedCruiseSpeed) ? throttlePosition : savedCruiseSpeed;
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
