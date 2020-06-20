from flask import Flask, render_template, request, redirect, url_for
import requests
import tensorflow as tf
from cat_dog import evaluate_model
app = Flask(__name__)
tflite_model_file = './advance.tflite'
home = 'static/images/home.jpg'
@app.route('/', methods=['GET', 'POST'])
def index():
    if request.method == 'POST':
        input_image = request.form.get('image')
        input_image = './static/data/' + input_image.split('/')[-1]
        img = 'static/data/' + input_image.split('/')[-1]
        interpreter = tf.lite.Interpreter(model_path=tflite_model_file)
        interpreter.allocate_tensors()
        predict = evaluate_model(interpreter, input_image)
        predict = "Predict result is: " + predict
        return render_template('index.html', img=img, predict=predict, home=home)
    return render_template('index.html', home=home)


if __name__ == '__main__':
    app.run()