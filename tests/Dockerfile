FROM python:alpine
RUN apk update && apk upgrade

WORKDIR /tests
COPY . .

ENTRYPOINT ["python", "tcp_server_test.py"]