#!/usr/bin/env python3
import socket
import threading
import queue
import json
import netifaces as ni
import time

from faceprint_utils import compute_similarity

FACE_SIMILARITY_MATCH_THRESHOLD = 0.5

# The port used by the server
PORT = 40000
# HOST_IP = '0.0.0.0'
device_interface = 'wlp0s20f3'
HOST_IP = ni.ifaddresses(device_interface)[ni.AF_INET][0]['addr']

# Create a queue to store the JSON objects
rx_queue = queue.Queue()

faceprints_map = dict()

MAX_JSON_STR_LEN = 40000  # Change this according to your needs
JSON_STR_START = '======START_JSON_MESSAGE======'
JSON_STR_END = '------END_JSON_MESSAGE------'

def handle_client(conn, addr):
    print('handle_client(): Connected client: ', addr)
    data_parts = ""
    try:
        while True:
            data = conn.recv(1024)

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
# def handle_client(conn, addr):
#     print('Connected client: ', addr)
#     while True:
#         data = conn.recv(1024)
#         if not data:
#             break
#         data = data.decode('utf-8') # Decode the data from bytes to string
#         rx_queue.put(data) # Put the JSON object into the queue
#         print(f"Received data from {addr}; pushed to rx queue.")
#     conn.close()

def process_received_data():

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
                        # Compute time delta and remove faceprint from faceprints_map
                        time_delta = epoch_time - face_epoch
                        print(f"process_received_data(): Time delta: {time_delta}")
                        del faceprints_map[face_epoch]

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

def start_server():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        print(f'Bind server to {HOST_IP}:{PORT}')
        s.bind((HOST_IP, PORT))
        s.listen()
        print(f'Server listening on {HOST_IP}:{PORT}')

        # Start a thread that will process JSON objects
        processing_thread = threading.Thread(target=process_received_data)
        processing_thread.start()

        while True:
            conn, addr = s.accept()
            thread = threading.Thread(target=handle_client, args=(conn, addr))
            thread.start()

if __name__ == "__main__":
    start_server()
