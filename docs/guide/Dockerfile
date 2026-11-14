FROM python:3.12-slim

WORKDIR /project
RUN apt-get update && apt-get install -y --no-install-recommends graphviz doxygen \
    && rm -rf /var/lib/apt/lists/*
COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt

ENTRYPOINT ["python", "-B", "/project/docs/build.py"]
