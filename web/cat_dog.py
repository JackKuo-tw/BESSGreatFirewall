import tensorflow as tf
from tensorflow import keras
import numpy as np
import os
import cv2

def evaluate_model(interpreter, input_image):

    input_index = interpreter.get_input_details()[0]["index"]
    output_index = interpreter.get_output_details()[0]["index"]

  # Run predictions on every image in the "test" dataset.
    prediction_digits = []

    img = cv2.imread(input_image, cv2.IMREAD_UNCHANGED)
    test_image = cv2.resize(img, (112, 112))
    test_image = test_image / 255.0
    x = np.expand_dims(test_image, axis=0).astype(np.float32)
    
    interpreter.set_tensor(input_index, x)

    # Run inference.
    interpreter.invoke()

    output = interpreter.tensor(output_index)
    category = 1 if output()[0] >= 0.5 else 0
    if category == 0:
        return "cat"
    else:
        return "dog"



