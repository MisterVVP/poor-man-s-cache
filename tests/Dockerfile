FROM python:alpine
RUN apk update && apk upgrade

WORKDIR /tests
COPY . .

ENTRYPOINT ["python", "websocket_test.py"]