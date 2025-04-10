from fastapi import FastAPI, WebSocket
from pydantic import BaseModel
from app.llm_client import get_llm_response
from app.post_processor import process_response

app = FastAPI()


class Request(BaseModel):
    session_id: str
    input: str


class Response(BaseModel):
    response: str
    timestamp: int


@app.post("/generate", response_model=Response)
async def generate_response(req: Request):
    raw_output = await get_llm_response(req.input, req.session_id)
    clean_output = process_response(raw_output)
    return {"response": clean_output, "timestamp": __import__("time").time()}


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    while True:
        data = await ws.receive_text()
        raw_output = await get_llm_response(data, session_id="ws-session")
        clean_output = process_response(raw_output)
        await ws.send_json(
            {"response": clean_output, "timestamp": __import__("time").time()}
        )
