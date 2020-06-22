import requests
import tensorflow as tf

from random import sample
from flask import Flask, render_template, request, redirect, url_for
from cat_dog import evaluate_model
from os import listdir
from os.path import isfile, join
from urllib.parse import urlparse

app = Flask(__name__)
app.config['TEMPLATES_AUTO_RELOAD'] = True
app.jinja_env.auto_reload = True

STATICS_FILE_NAME = 'statics.txt'


def error():
    return 'error'


def write_statics(url, txt):
    from datetime import datetime
    with open(STATICS_FILE_NAME, 'a') as f:
        f.write(f"{str(datetime.now())} | {url} | {txt}\n")


@app.route('/', methods=['GET'])
def index():
    text = request.args.get('text')
    if text == None:
        text = 'Give Me Dog'
    path = 'static/data/'
    images = [f for f in listdir(path) if isfile(join(path, f))]
    img_path = path + sample(images, 1)[0]
    return render_template('index.html', img=img_path, text=text)


@app.route('/examine/', methods=['GET'])
def examine():
    tflite_model_file = './advance.tflite'
    url = request.args.get('url')
    if url == None:
        return error()
    input_image = urlparse(url).path
    if input_image == '' or input_image.rfind('/') == -1:
        return error()
    img = './static/data/' + input_image[input_image.rfind('/')+1:]
    interpreter = tf.lite.Interpreter(model_path=tflite_model_file)
    interpreter.allocate_tensors()
    predict = evaluate_model(interpreter, img)
    result = "Predict result is: " + predict
    write_statics(url, predict)
    return result


@app.route('/statics/', methods=['GET'])
def statics():
    try:
        with open('statics.txt') as f:
            content = f.readlines()
    except FileNotFoundError:
        return "No data"
    rows = list()
    for i in content:
        rows.append(i.split('|'))
    rows.reverse()
    return render_template('statics.html', rows=rows)


if __name__ == '__main__':
    app.run()
