FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY sidecar.py .

EXPOSE 8790
CMD ["python3", "sidecar.py", "--host", "0.0.0.0", "--port", "8790", "--model", "/models/madlad400-3b-ct2-int8"]
