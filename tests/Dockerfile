FROM python:alpine
RUN apk update && apk upgrade && pip install requests

WORKDIR /tests
COPY . .

ENTRYPOINT ["python", "websocket_test.py"]