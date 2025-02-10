FROM python:alpine
RUN apk update && apk upgrade && pip install redis

WORKDIR /tests
COPY . .

ENTRYPOINT ["python", "tcp_server_test.py"]