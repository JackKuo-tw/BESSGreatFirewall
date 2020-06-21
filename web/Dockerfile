FROM python:3.7.2-stretch

WORKDIR /app

ADD . /app

RUN pip install --upgrade pip
RUN pip install -r requirements.txt

CMD python manage.py runserver --host 0.0.0.0 --port 80
