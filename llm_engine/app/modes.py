from pydantic import BaseModel


class Request(BaseModel):
    session_id: str
    input: str


class Response(BaseModel):
    response: str
    timestamp: int
