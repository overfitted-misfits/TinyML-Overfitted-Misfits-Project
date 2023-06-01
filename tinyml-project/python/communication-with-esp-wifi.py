#!/usr/bin/env python3
import socket
import threading
import queue
import json
import netifaces as ni
import time
import signal
import os
import sys

from faceprint_utils import compute_similarity
from average_calculations import rolling_avg
from average_calculations import exponential_moving_avg

# List of all connected sockets/clients
server = None
connections = []

# Minimum similarity between two faces to be considered a match
FACE_SIMILARITY_MATCH_THRESHOLD = 0.5

# The port used by the server
PORT = 40000

# Set the HOST (this devices) IP address. Attempt to do it automatically
# HOST_IP = '0.0.0.0'
device_interface = 'wlp0s20f3'
HOST_IP = ni.ifaddresses(device_interface)[ni.AF_INET][0]['addr']

# Create a queue to store the JSON objects
rx_queue = queue.Queue()

# Datastructure of the faceprints
faceprints_map = dict()

# Max possible JSON string to receive
MAX_JSON_STR_LEN = 40000  # Change this according to your needs

# JSON string start and stop markers/headers
JSON_STR_START = '======START_JSON_MESSAGE======'
JSON_STR_END = '------END_JSON_MESSAGE------'




def handle_client(conn, addr):
    """This run as a thread for each connected socket client. It receives data from the socket
    and, for each fully received JSON message, it adds it to the rx_queue to be processed later by process_received_data().

    Args:
        conn (_type_): connection/socket handle
        addr (_type_): Tuple of address bound to client's socket handle
    """
    print('handle_client(): Connected client: ', addr)
    data_parts = ""
    try:
        while True:
            data = conn.recv(1024)
            # Need better detection of broken connections
            if not data:
                break

            data_parts += data.decode('utf-8') # Decode the data from bytes to string

            while JSON_STR_END in data_parts:  # While there's at least one complete message in data_parts
                json_end = data_parts.find(JSON_STR_END) + len(JSON_STR_END)  # Find the end of the first complete message
                json_message = data_parts[:json_end]  # Extract the complete message
                start_index = json_message.find(JSON_STR_START)

                if start_index != -1 and start_index < json_end:  # If the start marker exists and is placed correctly

                    json_message = json_message.replace(JSON_STR_START, '').replace(JSON_STR_END, '')  # Remove start and end markers

                    if len(json_message) <= MAX_JSON_STR_LEN:  # If the message is not too long
                        rx_queue.put(json_message)  # Put the raw string into the queue
                        print(f"handle_client(): Received data from {addr}, added to queue.")
                    else:
                        print(f"handle_client(): Data from {addr} is too long, discarding.")

                else:
                    print(f"handle_client(): Data from {addr} has misplaced start marker, discarding.")
                data_parts = data_parts[json_end:]  # Remove the processed message from data_parts

            if len(data_parts) > MAX_JSON_STR_LEN:  # If data_parts is too long without having a complete message, reset it
                print(f"handle_client(): Incomplete message from {addr} is too long, discarding.")
                data_parts = ""
    except Exception as e:
        print(f"handle_client(): Connection with {addr} closed unexpectedly. Error: {e}")
    finally:
        conn.close()

def process_received_data():
    """Processes each fully received JSON string in rx_queue.
    Each JSON object should have the following structure:
    ```
    {
        "device_id": int,
        "data_type": "string",
        "data_length": int,
        "data": []
    }
    ```
    For example, a valid JSON object should look like this:
    ```
    {
        "device_id": 0,
        "data_type": "float",
        "data_length": 5,
        "data": [1.1 2.2 3.3 4.4 5.5]
    }
    ```

    Each receded JSON string is converted to a JSON object and is
    1. added to the faceprints_map if it is from device 0 and has not been seen before
    2. deleted from the faceprints_map if it is NOT from device 0 and has been seen before. Additionally, a time delta is computed from start and end detection times
    3. skipped if received from the same device as recorded already int he faceprints_map (ie duplicates)
    4. skipped if face was seen by the end device but not by the start device (ie, only faces from device 0 can be added to the faceprints_map)
    """
    last_epoch_time = 0
    while True:

        # Wait for and get the next JSON object from the queue
        json_str = rx_queue.get(block=True, timeout=None)
        print("\nprocess_received_data():==========================================================")

        # Because time is the key in the map, loop if the current time is the same as the last time
        # Should never happen because time is in nanoseconds
        epoch_time = int(time.time_ns())
        while epoch_time == last_epoch_time:
            epoch_time = int(time.time_ns())

        last_epoch_time = epoch_time

        newFace = None
        # Convert JSON string to a JSON object
        try:
            newFace = json.loads(json_str) # Load the JSON from the received string
        except Exception as e:
            # Failed to parse string as a JSON object (string is not JSON string)
            print(f"process_received_data(): Exception: \n{e}\n")
            print("process_received_data(): Received data is not a valid JSON object!!")
            print("process_received_data(): Raw string received: " + json_str)
            print("process_received_data(): Received data is not a valid JSON object!!")

            # Go to next iteration
            continue

        # String was successfully parsed as a JSON object
        # print("process_received_data(): Processed JSON faceprint!")
        newFaceprint = newFace['data']

        if newFace['data_length'] != 512:
            print("process_received_data(): Faceprint is not 512 bytes long, discard!")
            continue

        if newFace['data_type'] != "float":
            print("WARNING: process_received_data(): data type is not of type float!")

        numExistingFaceprints = len(faceprints_map)
        print(f"process_received_data(): there are {numExistingFaceprints} faceprint(s) in the datastructure!")

        if numExistingFaceprints <= 0 and newFace['device_id'] == 0:
            # If faceprint map is empty, then add the new faceprint to the map
            print("process_received_data(): Add new faceprint to empty faceprints_map")
            faceprints_map[epoch_time] = newFace
        elif numExistingFaceprints <= 0 and newFace['device_id'] != 0:
            # If faceprint map is empty, and new faceprint is NOT from start device, skip...
            print("process_received_data(): Skipping adding new faceprint from non-begin device to empty faceprints_map")
        elif numExistingFaceprints > 0:

            # need to search all existing faceprints in map for a potential match
            newFaceMatchesInMapCount = 0
            for count, face_epoch in enumerate(list(faceprints_map)):
                existingFace = faceprints_map[face_epoch]
                existingFaceprint = existingFace['data']

                # Compute similarity between new faceprint and existing faceprint in map
                similarity = None
                try:
                    similarity = compute_similarity(existingFaceprint, newFaceprint)
                    print(f"process_received_data(): similarity={round(similarity*100,2)}% between new faceprint and existing faceprint #{count} in map")
                except Exception as e:
                    # Failed to parse string as a JSON object (string is not JSON string)
                    print(f"\nEXPECTION: process_received_data(): Exception: \n{e}\n")
                    print("FAIL: process_received_data(): failed to compute similarity between new faceprint and existing faceprint in map")
                    print("FAIL: process_received_data(): Raw JSON string received: " + json_str)

                    # Delete face from faceprint_map because faceprint may be invalid
                    del faceprints_map[face_epoch]
                    # Set matches non-zero value so that face does not get added to faceprints_map because we failed to check all faces in the map (and faceprint may be invalid)
                    newFaceMatchesInMapCount = 1

                    print("")
                    # Break out of for loop
                    break

                # If similar enough, then it is considered a match
                if similarity >= FACE_SIMILARITY_MATCH_THRESHOLD:
                    newFaceMatchesInMapCount += 1
                    print(f"process_received_data(): New faceprint is similar to existing faceprint (matched with {newFaceMatchesInMapCount} faceprints so far)...")

                    # If existingFace faceprint is a match, check the id of the device that detected the existingFace
                    # if the existing face 
                    if existingFace["device_id"] == 0 and newFace['device_id'] == 0:
                        # The device that detected the existingFace is the start/begin device
                        print(f"process_received_data(): Faceprint from device 0 already exists in faceprints_map from device 0...skip")
                        # break
                    elif existingFace["device_id"] != 0 and newFace['device_id'] == 0:
                        # This should never happen ideally! But false matches likely will cause it to happen, so skip
                        print(f"process_received_data(): Faceprint from non-start device exists, skip adding new faceprint from start device")
                        # break
                    elif existingFace["device_id"] == 0 and newFace['device_id'] != 0:
                        # The device that detected the existingFace is the end device
                        del faceprints_map[face_epoch]

                        # Compute time delta and remove faceprint from faceprints_map
                        time_delta_ns = epoch_time - face_epoch
                        time_delta_s = round(time_delta_ns / 1000000000, 2)
                        print(f"process_received_data(): Time delta: {time_delta_s}s")
                        rollingAvg = rolling_avg(time_delta_s)
                        print(f"process_received_data(): Time rolling avg: {round(rollingAvg,2)}s")
                        expMovingAvg = exponential_moving_avg(time_delta_s)
                        print(f"process_received_data(): Time exponential moving avg: {round(expMovingAvg,2)}s")

                        # Match found, break the out of the faceprint_map loop
                        # break
                    else: # existingFace["device_id"] != 0 and newFace['device_id'] != 0:
                        # Unhandled case
                        print(f"process_received_data(): faceprint not from start device matches faceprint not from start device...skip...")
                        # break
                else:
                    # print(f"process_received_data(): New faceprint is not similar to existing faceprint")
                    # continue to next iteration...
                    continue

            if newFaceMatchesInMapCount == 0 and newFace["device_id"] == 0:
                # newFace doesn't exist in faceprints_map, but is from start device
                # Add new face to faceprints_map
                print(f"process_received_data(): Add new faceprint to faceprints_map")
                faceprints_map[epoch_time] = newFace
        else:
            # This case will never be reached but added for clarity/safety
            print(f"process_received_data(): impossible case")

def close_all_connections(signal, frame):
    """signal handler for Ctrl+C
    Close all client connections and socket server

    Args:
        signal (_type_): signal type
        frame (_type_): 
    """
    print("\nCtrl+C detected. Closing all client connections.")
    for conn in connections:
        conn.close()
    print("All client connections closed.")

    print("Closing server socket.")
    if server is not None:
        server.close()
    print("Server socket closed.")

    # sys.exit(0)

    # Force fill via process id python python sucks and has bugs and can't seem to kill itself with threads any other way
    os.system('kill %d' % os.getpid())
    # os._exit(0)
    
    ## Note that python still sucks and can't actually free the socket it's using

def start_server():
    """main function; this will start the socket server, accept client connections, start threads to receive and process JSON strings, etc
    """
    # Register the signal handler
    signal.signal(signal.SIGINT, close_all_connections)

    print(f'Bind server to {HOST_IP}:{PORT}')
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind((HOST_IP, PORT))
    server.listen()

    print(f'Server listening on {HOST_IP}:{PORT}')

    # Start a thread that will process JSON strings received from clients
    print(f'Start processing_thread to process received JSON strings')
    processing_thread = threading.Thread(target=process_received_data)
    processing_thread.start()

    print(f'Accept client connections...')
    while True:
        conn, addr = server.accept()
        connections.append(conn)
        print('Accepted new connection')
        thread = threading.Thread(target=handle_client, args=(conn, addr))
        thread.start()

if __name__ == "__main__":
    start_server()
