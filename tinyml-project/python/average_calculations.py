from collections import deque

"""
This module contains functions that compute a rolling average and an 
exponential moving average (EMA) of the given numbers.
"""

# Rolling Average
ROLLING_AVG_CONTEXT = 100
data_queue = deque(maxlen=ROLLING_AVG_CONTEXT)

def rolling_avg(new_val):
    """
    Compute a rolling average of the given numbers.

    This function adds a new number to the queue of recent numbers and returns 
    the average of the numbers in the queue. If the queue is already full, the 
    oldest number is automatically removed.

    Args:
        new_val (float): A new number to include in the average.

    Returns:
        float: The current rolling average.
    """
    global data_queue  # use the global queue
    data_queue.append(new_val)  # add new value to the queue
    return sum(data_queue) / len(data_queue)  # compute and return the average


# Exponential Moving Average
EMA_CONTEXT = 100 # replace with the number you want
alpha = 2 / (EMA_CONTEXT + 1)  # smoothing factor for EMA
current_avg = None

def exponential_moving_avg(new_val):
    """
    Compute an exponential moving average (EMA) of the given numbers.
    
    This function updates the current average to be a weighted average of the 
    new number and the current average. The weight of the new number is 
    determined by alpha, and the weight of the current average is 1 - alpha.

    Args:
        new_val (float): A new number to include in the average.

    Returns:
        float: The updated average.
    """
    global current_avg  # use the global current average
    if current_avg is None:
        # if it's the first number, initialize the average with it
        current_avg = new_val
    else:
        # compute the new average
        current_avg = alpha * new_val + (1 - alpha) * current_avg
    return current_avg
