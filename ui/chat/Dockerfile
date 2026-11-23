FROM python:3.12-slim
WORKDIR /app
COPY requirements_3.12.lock /tmp/base.lock
COPY ai/models/decisions/requirements_3.12.lock /tmp/decisions.lock
RUN grep -E '^[a-zA-Z0-9_.-]+==' /tmp/base.lock > /tmp/base.constraints \
    && pip install --no-cache-dir -c /tmp/base.constraints aiohttp \
    && pip install --no-cache-dir -r /tmp/decisions.lock \
    && pip check
COPY ai/models/decisions/choice.py ai/models/decisions/
COPY ui/chat/server.py ui/chat/
USER nobody
CMD ["python", "-m", "ui.chat.server"]
