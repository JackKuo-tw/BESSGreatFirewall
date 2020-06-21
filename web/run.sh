#!/usr/bin/env bash

docker build -t web .
docker run -i -d --net=none --name=vport_test web python manage.py runserver --host 0.0.0.0 --port 80
