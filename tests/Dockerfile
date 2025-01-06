FROM python:alpine
RUN apk update && apk upgrade && pip install websocket-client

WORKDIR /tests
COPY . .

ENTRYPOINT ["python", "websocket_test.py"]