#!/bin/bash

OUTPUT_FILE=~/Desktop/Input.json

TARGET=$(whiptail --title "Choose your target." \
--radiolist "Please choose your target. \n-Hover over your choice and press space to choose. \n-Press enter to confirm choice. \n-Press tab to choose between 'OK' and 'CANCEL' " 0 0 0 \
"ARM" "ARM Cortex-M" ON \
"AVR" "Atmel AVR" OFF \
"TRICORE" "Infineon TRICORE" OFF  3>&1 1>&2 2>&3)

# Exit if canceled
[ $? -ne 0 ] && exit 1

# Ask the user how many faults will they inject
NUM_FAULTS=$(whiptail --inputbox "How many faults do you want to inject?" 0 0 --title "Number of faults." 3>&1 1>&2 2>&3)

# Exit if canceled
[ $? -ne 0 ] && exit 1

# Start JSON file
echo '{ "faults": [' > "$OUTPUT_FILE"



for ((i=1; i<=NUM_FAULTS; i++)); do

    FAULT=$(whiptail --title "Fault type." \
    --radiolist "Choose your fault type. \n-Hover over your choice and press space to choose. \n-Press enter to confirm choice." 0 0 0 \
    "BIT FLIP" "specific bit is changed" ON \
    "SENSOR CORRUPTION" "invalid data from a sensor" OFF \
    "MEMORY CORRUPTION" "change a whole register" OFF \
    "SET PC" "jump the program counter" OFF  3>&1 1>&2 2>&3)






    if [ "$FAULT" = "BIT FLIP" ]; then

    ADDRESS=$(whiptail --inputbox "Where do you want to inject?" 0 0 --title "Location of injection." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    TIME=$(whiptail --inputbox "When do you want to inject? \n-Time in uSec" \
     0 0 --title "Time of injection." 3>&1 1>&2 2>&3)
    
    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    BIT=$(whiptail --inputbox "Which bit?" 0 0 --title "Bit." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

        cat >> "$OUTPUT_FILE" <<EOF
        {
            "Target": "$TARGET",
            "id": $i,
            "fault_type": "bit_flip",
            "address": "$ADDRESS",
            "time": $TIME,
            "bit": $BIT
        }
EOF
    # Add comma except for last entry
    if [ "$i" -lt "$NUM_FAULTS" ]; then
        echo "," >> "$OUTPUT_FILE"
    fi


    fi





    if [ "$FAULT" = "SENSOR CORRUPTION" ]; then

    SENSOR=$(whiptail --inputbox "Which sensor?" 0 0 --title "Sensor." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    MODE=$(whiptail --inputbox "What do you want to do to the sensor?" 0 0 --title "Corruption mode." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    VALUE=$(whiptail --inputbox "What value do you want to input?" 0 0 --title "Corruption value." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    START_TIME=$(whiptail --inputbox "When do you want to start corruption? \n-Time in uSec" 0 0 --title "Start time." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    DURATION=$(whiptail --inputbox "How long do you want the corruption to be? \n-Time in uSec" 0 0 --title "Duration." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    cat >> "$OUTPUT_FILE" <<EOF
        {
            "Target": "$TARGET",
            "id": $i,
            "fault_type": "sensor_corruption",
            "sensor": "$SENSOR",
            "value": $VALUE,
            "mode": "$MODE",
            "start_time": $START_TIME,
            "duration": $DURATION
        }
EOF

    # Add comma except for last entry
    if [ "$i" -lt "$NUM_FAULTS" ]; then
        echo "," >> "$OUTPUT_FILE"
    fi


    fi





    if [ "$FAULT" = "MEMORY CORRUPTION" ]; then
    
    ADDRESS=$(whiptail --inputbox "Where do you want to Inject?" 0 0 --title "Location of injection." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    VALUE=$(whiptail --inputbox "What value do you want to input?" 0 0 --title "Corruption value." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    cat >> "$OUTPUT_FILE" <<EOF
        {
            "Target": "$TARGET",
            "id": $i,
            "fault_type": "memory_corruption",
            "address": "$ADDRESS",
            "value": $VALUE
        }
EOF

    # Add comma except for last entry
    if [ "$i" -lt "$NUM_FAULTS" ]; then
        echo "," >> "$OUTPUT_FILE"
    fi


    fi






    if [ "$FAULT" = "SET PC" ]; then

    JUMP_TIME=$(whiptail --inputbox "When do you want to jump? \n-Time in uSec" 0 0 --title "Jump time." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    LOCATION=$(whiptail --inputbox "Where do you want to jump?" 0 0 --title "Jumping location." 3>&1 1>&2 2>&3)

    # Exit if canceled
    [ $? -ne 0 ] && exit 1

    cat >> "$OUTPUT_FILE" <<EOF
        {
            "Target": "$TARGET",
            "id": $i,
            "fault_type": "setpc",
            "time": "$JUMP_TIME",
            "location": "$LOCATION"
        }
EOF

    # Add comma except for last entry
    if [ "$i" -lt "$NUM_FAULTS" ]; then
        echo "," >> "$OUTPUT_FILE"
    fi


    fi

done

# Close JSON
echo '] }' >> "$OUTPUT_FILE"