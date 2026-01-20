#!/bin/bash

OUTPUT_FILE=./Input.json



INJECTION=$(whiptail --title "Choose Your Injection Mode." \
--radiolist "Choose your mode" 0 0 0 \
"HARDWARE" "Real Hardware" ON \
"QEMU" "Emulated Hardware" OFF 3>&1 1>&2 2>&3)

# Exit if canceled
[ $? -ne 0 ] && exit 1



FIRMWARE=$(whiptail --inputbox "Enter you application(file) name" --title "Firmware" 0 0 3>&1 1>&2 2>&3)

# Exit if canceled
[ $? -ne 0 ] && exit 1



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

    CONFIRMED=false
    NUMBER=false

        while [ "$CONFIRMED" = false ]; do
        
            ADDRESS=$(whiptail --inputbox "Where do you want to inject?" 0 0 --title "Location of injection." 3>&1 1>&2 2>&3)

            # Exit if canceled
            [ $? -ne 0 ] && exit 1


            while [ "$NUMBER" = false ]; do

                TIME=$(whiptail --inputbox "When do you want to inject? \n-Time in uSec" \
                0 0 --title "Time of injection." 3>&1 1>&2 2>&3)
                
                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ ! "$TIME" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a number only" 0 0
                else
                    NUMBER=true
                fi

            done

            NUMBER=false

            while [ "$NUMBER" = false ]; do

                BIT=$(whiptail --inputbox "Which bit?" 0 0 --title "Bit." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ ! "$BIT" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a number only" 0 0
                else
                    NUMBER=true
                fi
            done

            NUMBER=false

            if whiptail --title "Values Confirmation" --yesno "Are you sure of these values? \n Adress: "$ADDRESS" \n Time of injection: $TIME \n Bit: $BIT " 0 0; then

                CONFIRMED=true 

                cat >> "$OUTPUT_FILE" <<EOF
                {
                    "Firmware": "$FIRMWARE.elf",
                    "Mode": "$INJECTION",
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
            else
                [ $? -eq 255 ] && exit 1
            fi

        done

    fi




    if [ "$FAULT" = "SENSOR CORRUPTION" ]; then

    CONFIRMED=false
    NUMBER=false

        while [ "$CONFIRMED" = false ]; do

            while [ "$NUMBER" = false ]; do

                SENSOR=$(whiptail --inputbox "Which sensor?" 0 0 --title "Sensor." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ "$SENSOR" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a letters only" 0 0
                else
                    NUMBER=true
                fi
            done

            NUMBER=false

            while [ "$NUMBER" = false ]; do

                MODE=$(whiptail --inputbox "What do you want to do to the sensor?" 0 0 --title "Corruption mode." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ "$MODE" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a letters only" 0 0
                else
                    NUMBER=true
                fi
            done

            NUMBER=false

            while [ "$NUMBER" = false ]; do

                VALUE=$(whiptail --inputbox "What value do you want to input?" 0 0 --title "Corruption value." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ ! "$VALUE" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a numbers only" 0 0
                else
                    NUMBER=true
                fi
            done

            NUMBER=false

            while [ "$NUMBER" = false ]; do

                START_TIME=$(whiptail --inputbox "When do you want to start corruption? \n-Time in uSec" 0 0 --title "Start time." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ ! "$START_TIME" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a numbers only" 0 0
                else
                    NUMBER=true
                fi
            done

            NUMBER=false

            while [ "$NUMBER" = false ]; do

                DURATION=$(whiptail --inputbox "How long do you want the corruption to be? \n-Time in uSec" 0 0 --title "Duration." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ ! "$DURATION" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a numbers only" 0 0
                else
                    NUMBER=true
                fi
            done

            NUMBER=false

            if whiptail --title "Values Confirmation" --yesno "Are you sure of these values? \n Sensor: "$SENSOR" \n Mode: $MODE \n Value: $VALUE \n Start time: $START_TIME \n Duration: $DURATION " 0 0; then

                CONFIRMED=true

                    cat >> "$OUTPUT_FILE" <<EOF
                        {
                            "Firmware": "$FIRMWARE.elf",
                            "Mode": "$INJECTION",
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
                else
                    [ $? -eq 255 ] && exit 1
                fi

            done
    fi




    if [ "$FAULT" = "MEMORY CORRUPTION" ]; then

    CONFIRMED=false
    NUMBER=false

        while [ "$CONFIRMED" = false ]; do


            while [ "$NUMBER" = false ]; do
                MIN=$(whiptail --inputbox "What is the min value that needs to be checked?" 0 0 --title "Minimum value." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1  

                if [[ ! "$MIN" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a numbers only" 0 0
                else
                    NUMBER=true
                fi
            done

            NUMBER=false

            while [ "$NUMBER" = false ]; do

                MAX=$(whiptail --inputbox "What is the max value that needs to be checked?" 0 0 --title "Maximum value." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ ! "$MAX" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a numbers only" 0 0
                else
                    NUMBER=true
                fi
            done

            NUMBER=false

                ADDRESS=$(whiptail --inputbox "Where do you want to Inject?" 0 0 --title "Location of injection." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1


            while [ "$NUMBER" = false ]; do

                VALUE=$(whiptail --inputbox "What value do you want to input?" 0 0 --title "Corruption value." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ ! "$VALUE" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a numbers only" 0 0
                else
                    NUMBER=true
                fi
            done

            if whiptail --title "Values Confirmation" --yesno "Are you sure of these values? \n Adress: "$ADDRESS" \n Value: $VALUE" 0 0; then

                CONFIRMED=true 

                cat >> "$OUTPUT_FILE" <<EOF
                    {
                        "Firmware": "$FIRMWARE.elf",
                        "Mode": "$INJECTION",
                        "Target": "$TARGET",
                        "id": $i,
                        "fault_type": "memory_corruption",
                        "max": $MAX,
                        "min": $MIN,
                        "address": "$ADDRESS",
                        "value": "$VALUE"
                    }
EOF
                # Add comma except for last entry
                if [ "$i" -lt "$NUM_FAULTS" ]; then
                    echo "," >> "$OUTPUT_FILE"
                fi
            else
                [ $? -eq 255 ] && exit 1
            fi
            
        done

    fi



    if [ "$FAULT" = "SET PC" ]; then

    CONFIRMED=false
    NUMBER=false

        while [ "$CONFIRMED" = false ]; do

            while [ "$NUMBER" = false ]; do

                JUMP_TIME=$(whiptail --inputbox "When do you want to jump? \n-Time in uSec" 0 0 --title "Jump time." 3>&1 1>&2 2>&3)

                # Exit if canceled
                [ $? -ne 0 ] && exit 1

                if [[ ! "$JUMP_TIME" =~ ^[0-9]+$ ]];then
                    whiptail --msgbox "Please input a numbers only" 0 0
                else
                    NUMBER=true
                fi
            done

            LOCATION=$(whiptail --inputbox "Where do you want to jump?" 0 0 --title "Jumping location." 3>&1 1>&2 2>&3)

            # Exit if canceled
            [ $? -ne 0 ] && exit 1

            if whiptail --title "Values Confirmation" --yesno "Are you sure of these values? \n Time of jumping: "$JUMP_TIME" \n Location: $LOCATION " 0 0; then

                CONFIRMED=true 

                cat >> "$OUTPUT_FILE" <<EOF
                    {
                        "Firmware": "$FIRMWARE.elf",
                        "Mode": "$INJECTION",
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
            else
                [ $? -eq 255 ] && exit 1
            fi
        done

    fi

done

# Close JSON
echo '] }' >> "$OUTPUT_FILE"