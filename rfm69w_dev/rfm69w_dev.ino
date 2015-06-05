// rfm69w_dev.ino - An Arduino script to develop with RFM69W
// ---------------------------------------------------------------------
// Author: M. Tunstall
// NOTE: This is heavily commented for my own learning/reference.


#include <Wire.h>       // Used for serial comms.
#include <stdint.h>     // Enable fixed width integers.
#include <avr/wdt.h>    // Includes Inline Macros for working with the WDT
#include <avr/sleep.h>  // Includes Inline Macros for working with Sleep Modes
#include <avr/interrupt.h>  // Required for interrupts.
#include <avr/power.h>  // Power reduction management.
#include "spi.h"        // Include my spi library.
#include "rfm69w.h"     // Include my rfm69w library
#include "rfm69w_reg.h"     // Register reference for rfm69w

#define DEBUG  // Enables Debugging code. Comment out to disable debug code.

// Function Declarations
void setup_int();
void listen();
void setup_mode();
void setupRFM();
void powerSave();
void gotosleep();
void setup_wdt();

typedef Spi SPIx;                // Create Global instance of the Spi Class
RFM69W<SPIx> RFM;        // Create Global instance of RFM69W Class
volatile uint8_t intFlag = 0x00;  // Setup a flag for monitoring the interrupt.
volatile uint8_t wdtFlag = 0x00;  // Setup a flag for monitoring WDT interrupt.
uint8_t mode = 0x00;     // Node startup mode. Rx Default.
void setup() {
    
    // Set PB0 as Tx/Rx Mode select input
    DDRB &= ~(1 << DDB0);
    // No internal pullup on PB0, hardwired to VCC (Tx) or GND (Rx).
    PORTB &= ~(1 << PORTB0);
    // TODO: Move this to a more relevent place as Serial comms need to be
    //       disabled on the Tx Node for power saving.
    Serial.begin(19200);  // Setup Serial Comms
    // TODO: Test with delay removed, probably not required. 
    //delay(2000);   // Wait before entering loop
    
    // DEV Note: Will startup power requirements benefit from reordering of the 
    //           powerSave(), RFM.setReg(), setupRFM() functions?
    //           The RFM module is likely to be the biggest power draw until it 
    //           makes it into sleep mode.
    powerSave();    // Enable powersaving features
    RFM.setReg();  // Setup the registers & initial mode for the RFM69
    setupRFM();    // Application Specific Settings RFM69W
    setup_mode();  // Determine the startup mode from status of PB0.

    setup_int();   // Setup Interrupts
    setup_wdt();   // Setup WDT Timeout Interrupt
    sei();  // Enable interrupts
}
void powerSave() {
    power_adc_disable(); // Not using ADC
    power_twi_disable(); // Not using I2C
    power_timer0_disable();
    power_timer1_disable();
    power_timer2_disable();
    
    // Disable Interrupts
    // Note: No need as they are not enabled until after the powerSave function is used.
    ACSR &= (1<<ACD); // Disable the analogue comparator
    ACSR |= ~(1<<ACI);// Clear the analogue comparator interrupt if it was trigged from the disable command.
    // Enable Interrupts
    // Note: No need in this case.
    
    
    //power_usart0_disable(); // ToDo: Enable this later. Using for debugging.

}

void setupRFM() {
    // Write Custom Setup Values to registers

    // Data Modulation
    // - Packet Mode, OOK, No Shaping
    RFM.singleByteWrite(RegDataModul, 0x08);

    // DIO0 Mapping - Starup value, want to change during operation
    //              - depending on mode
    // TODO: Confirm best initial state.
    RFM.singleByteWrite(RegDioMapping1, 0x00);

    // Packet Config - Set Fixed Length 8 bytes
    // singleByteWrite(RegDataModul,0x10);  // Def Fixed Packet(Default)
    // Set Fixed Packet Length to 8 bytes.
    RFM.singleByteWrite(RegPayloadLength, 0x08);

    // Set Carrier Frequency to 867,999,975.2 Hz
    RFM.singleByteWrite(RegFrfMsb, 0xd9);
    RFM.singleByteWrite(RegFrfMid, 0x00);
    RFM.singleByteWrite(RegFrfLsb, 0x24);

    // Set DIO4/5, Disable Clk Out - None of these used/connected
    RFM.singleByteWrite(RegDioMapping2, 0x07);
}

void setup_mode() {
    // Configure the node startup mode as a Tx or Rx.
    if (PINB & (1 << PINB0)) {
        // Tx Mode Selected
        mode = 0xff;  // Change node mode
        #ifdef DEBUG
        Serial.println("Tx Mode");  // DEBUG: Print "Tx Mode"
        #endif  // DEBUG
        // RFM69W configured to startup in sleep mode and will wake to
        // transmit as required.
        // TODO: Check interrupt settings / DIO0 map for sleep mode
    } else {
        // Rx Mode Selected
        #ifdef DEBUG
        Serial.println("Rx Mode");  // DEBUG: Print "Rx Mode"
        #endif
        RFM.modeReceive();
    }
    return;
}

void setup_wdt() {
    // Clear WDRF - Watchdog System Reset Flag to allow WDE to be cleared later.
    MCUSR &= ~(1<<WDRF);
    /*
    To perform adjustments to WDE & prescaler bits:
    - Set the WDCE - watchdog change enable bit 
    - Make adjustments within 4 clock cycles.
    */
    
    WDTCSR |= (1<<WDCE)| (1<<WDE); // Set WDCE
     
    /*
    WDTCSR Register
    - Bit 7 WDIF - Watchdog Interrupt Flag
    - Bit 6 WDIE - Watchdog Interrupt Enable
    - Bit 5 WDP3 - Watchdog Timer Prescaler 3
    - Bit 4 WDCE - Watchdog Change Enable
    - Bit 3 WDE  - Watchdog System Reset Enable
    - Bit 2 WDP2 - Watchdog Timer Prescaler 2
    - Bit 1 WDP1 - Watchdog Timer Prescaler 1
    - Bit 0 WDP0 - Watchdog Timer Prescaler 0
         
    Setup for Interrupt Mode, 8 second timeout
    ------------------------------------------
    Prescaler Settings for Timeout of approx 8 seconds.
    WDP3 = 1, WDP2 = 0, WDP1 = 0, WDP0 = 1
    
    To Set Interrupt Mode
    WDE = 0, WDIE = 1 // Toggle WDIE later to enable/disable interrupt mode.
    
    Other Flags
    WDCE = 1
    WDIF = 1 // Normally cleared by HW, write 1 to clear
    
    Note: In this mode the system reset is disabled.
    */
           
    // Set Watch1dog Timer for Interrupt Mode, 8 second timeout.
    WDTCSR = (1<<WDP3)|(0<<WDP2)|(0<<WDP1)|(1<<WDP0)|
             (0<<WDE)|(1<<WDIE)|
             (1<<WDCE)|(1<<WDIF);
}

void gotosleep(){
    // Select the sleep mode to be use and enable it.
    // In this case the Power-down mode is selected.
    //MCUCR |= (1<<SM1)|(1<<SE);
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_bod_disable();
    sleep_mode();
    sleep_disable();
    
}

void setup_int() {
    // Set PB1 as interrupt input.
    DDRB &= ~(1 << DDB1);
    // Not using internal pullup on PB1 as want to trigger on a logic 1
    // RFM69W DIO0 is logic 0 until set. Pull down resistor not used.
    PCICR |= (1 << PCIE0);  // Enable PCMSK0 covering PCINT[7:0]
    PCMSK0 |= (1 << PCINT1);  // Set mask to only interrupt on PCINT1
}

ISR(PCINT0_vect) {  // PCINT0 is vector for PCINT[7:0]
    // Dev Note: Serial.println() cmds can't be used in an ISR.
    /*
        The ISR will set a flag that can be tested by the main loop.
        The interrupt is triggered by DIO0 on RFM69W.
        Setting a local flag via this interrupt allows the monitoring of
        RFM69W without the need to constantly read the register statuses
        over the SPI bus.
    */
    intFlag = 0xff;  // Set interrupt flag.
}

ISR(WDT_vect) { // Runs when WDT timeout is reached
    // Dev Note: This ISR is intended only for waking
    // the mcu from a sleep mode. Speed of the ISR is not important in this case.
    wdtFlag = 0xFF;
}

void test_singleByteRead(uint8_t byteAddr, uint8_t byteExpect) {
    // SPI - singleByteRead
    Serial.println("SPI - singleByteRead");
    uint8_t datain = RFM.singleByteRead(byteAddr);
    Serial.print("ADDR: ");
    Serial.println(byteAddr, HEX);
    Serial.print("Expected Data: ");
    Serial.println(byteExpect, HEX);
    Serial.print("Actual Data: ");
    Serial.println(datain, HEX);
    Serial.println();
    delay(1000);
}

void test_spiReg() {
    // Print the SPI Registers to Serial Output
    Serial.println("SPI Registers");
    Serial.print("SPCR: ");
    Serial.println(SPCR, BIN);
    Serial.print("SPSR: ");
    Serial.println(SPSR, BIN);
    Serial.println();
    delay(1000);
}

void test_singleByteWrite(uint8_t byteAddr, uint8_t dataByte) {
    // SPI - singleByteWrite
    Serial.println("SPI - singleByteWrite");
    RFM.singleByteWrite(byteAddr, dataByte);
    Serial.print("ADDR: ");
    Serial.println(byteAddr, HEX);
    Serial.print("Sent Data: ");
    Serial.println(dataByte);
    Serial.println();
    delay(1000);
}

void test_Reg() {
    // Code used to test the register values on the rfm69w
    // Assumes working SPI connection
    Serial.println("Check Reg Init");
    test_singleByteRead(RegLna, 0x88);
    test_singleByteRead(RegRxBw, 0x55);
    test_singleByteRead(RegAfcBw, 0x8b);
    test_singleByteRead(RegDioMapping2, 0x07);
    test_singleByteRead(RegRssiThresh, 0xe4);
    test_singleByteRead(RegSyncValue1, 0x01);
    test_singleByteRead(RegSyncValue2, 0x01);
    test_singleByteRead(RegSyncValue3, 0x01);
    test_singleByteRead(RegSyncValue4, 0x01);
    test_singleByteRead(RegSyncValue5, 0x01);
    test_singleByteRead(RegSyncValue6, 0x01);
    test_singleByteRead(RegSyncValue7, 0x01);
    test_singleByteRead(RegSyncValue8, 0x01);
    test_singleByteRead(RegFifoThresh, 0x8f);
    test_singleByteRead(RegTestDagc, 0x30);
    Serial.println("Check Custom Reg Init");
    test_singleByteRead(RegDataModul, 0x08);
}

void test_SPI() {
    test_spiReg();
    test_singleByteRead(0x2d, 0x03);
    test_singleByteWrite(0x2d, 0x04);
    test_singleByteRead(0x2d, 0x04);
    test_singleByteWrite(0x2d, 0x03);
    Serial.println();
}

void ping(int8_t msg) {
    // Load selected data into FIFO Register for transmission

    // Sends teststring stored in array via RFM69W
    // Workout how many characters there are to send.
    // 1 is deducted from count to remove the trailing null char.
    uint8_t tststr[] = "ERROR!";
    uint8_t tststr0[] = "Hello_";
    uint8_t tststr1[] = "World!";
    // Dev Note: Should this result in 2 packets being received?
    //           Only detecting one.
    // - Update: Since fixed length packets are used the only the data
    //           that can be contained in the packet is sent.
    //           It appears the act of sending a single packet clears
    //           any residual data in the FIFO. If there is data for a
    //           follow on packet it should be sent as a separate Tx
    //           operation.
    uint8_t tststr2[] = "0123456789ABCDEF";

    switch (msg) {
    case 0:
        for (uint8_t arrayChar = 0; arrayChar < (sizeof(tststr0)-1); arrayChar++)
            RFM.singleByteWrite(RegFifo, tststr0[arrayChar]);
        break;
    case 1:
        for (uint8_t arrayChar = 0; arrayChar < (sizeof(tststr1)-1); arrayChar++)
            RFM.singleByteWrite(RegFifo, tststr1[arrayChar]);
        break;
    case 2:
        for (uint8_t arrayChar = 0; arrayChar < (sizeof(tststr2)-1); arrayChar++)
            RFM.singleByteWrite(RegFifo, tststr2[arrayChar]);
        break;
    default:
        for (uint8_t arrayChar = 0; arrayChar < (sizeof(tststr)-1); arrayChar++)
            RFM.singleByteWrite(RegFifo, tststr[arrayChar]);
    }
}

void listen() {
    // Listens for an incomming packet via RFM69W
    // Read the Payload Ready bit from RegIrqFlags2 to see if any data
    #ifdef DEBUG
    Serial.println("Start Listening: ");
    #endif // DEBUG
    while (RFM.singleByteRead(RegIrqFlags2) & 0x04) {
    // True whilst FIFO still contains data.
        Serial.print("Rec: ");
        Serial.println(RFM.singleByteRead(RegFifo));
    }
    #ifdef DEBUG
    Serial.println("Stop Listening.");
    #endif // DEBUG
    intFlag = 0x00;  // Reset interrupt flag
}

void transmit() {
    // The SPI communication and registers have been set by setup()
    #ifdef DEBUG
    Serial.println("Start: ");  // DEBUG: Print "Start: " Start of Tx.
    #endif  // DEBUG
    // The RFM69W should be in Sleep mode.
    // Load bytes to transmit into the FIFO register.
    ping(2);  // Pass an int to select which msg to send.
    // Data will be sent once the conditions for Tx have been met.
    // In packet mode & data already in the FIFO buffer this should
    // happen as soon as Tx mode is enabled.
    RFM.modeTransmit();  // Data being sent at 4.8kbps
    while (!(RFM.singleByteRead(RegIrqFlags2) & 0x08)) {
        // Keeps checking to see if the packet sent bit is set.
        // Once packet send is confirmed the program will continue.
        // The reason for the loop here is that to save power the
        // transmitter needs to be turned off as soon as possible after
        // the program has finished with it.
    }
    RFM.modeSleep();  // Return to Sleep mode to save power.
    #ifdef DEBUG
    Serial.println("End: ");  // DEBUG: Print "End: " End of Tx.
    #endif  // DEBUG
}

void transmitter() {
    // Transmitter Node Loop
    while (1) {
        transmit();
        // TODO: Enter Lower Power Mode between transmissions
        //wdt_reset(); // Reset the watchdog timer for full sleep cycle
        //delay(15000);  // Transmit a packet every 15 seconds.
        gotosleep();
        // TODO: Wakeup from low power mode before transmitting.
        // Execution resumes at this point after the ISR is triggered
    }
}

void receiver() {
    // Receiver Node Loop
    while (1) {
        // If the interrupt flag has been set then listen for incomming data.
        while (intFlag == 0xff) {
            listen();
        }
        #ifdef DEBUG
        // Serial.println("Loop Wait");  // DEBUG: Print "Loop Wait"
        #endif  // DEBUG
    }
}

void loop() {
    #ifdef DEBUG
    // test_SPI();  // DEBUG: Test SPI Comms
    // test_Reg();  // DEBUG: Test RFM69W Register Values
    #endif  // DEBUG
    if (mode == 0xff) {     // If node configured as a Transmitter.
        // Run transmitter node loop
        transmitter();
    } else {                // If node configured as a Receiver.
        // Run receiver node loop
        receiver();
    }
}
