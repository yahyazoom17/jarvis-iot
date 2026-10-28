from fastapi import FastAPI
from dotenv import load_dotenv
from jarvis_iot.schemas.QuerySchema import UserQuery

load_dotenv()  # Load environment variables from .env file

from jarvis_iot.utils import invoke_jarvis

app = FastAPI(
    title="Jarvis IoT Server",
    description="This is the server for the Jarvis IoT project.",
    version="0.1.0"
)

# Home routes

@app.get("/")
async def home():
    return {
        "status": "success",
        "status_code": 200,
        "message": "Welcome to the Jarvis IoT Server!"
    }

@app.get("/health")
async def health_check():
    return {
        "status": "success",
        "status_code": 200,
        "message": "Server is healthy!"
    }

# Agent routes

@app.post("/jarvis")
async def check_jarvis():
    return {
        "status": "success",
        "status_code": 200,
        "message": "Jarvis is ready to answer your questions!"
    }

@app.post("/jarvis/ask")
async def ask_jarvis(user_query: UserQuery):
    result = invoke_jarvis(user_query.query)
    return {
        "status": "success",
        "status_code": 200,
        "message": result
    }

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)