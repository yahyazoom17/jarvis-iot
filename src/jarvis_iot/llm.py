import os
from jarvis_iot.config import JARVIS_SYSTEM_PROMPT, JARVIS_USER_PROMPT
from litellm import completion

messages = [
    {
        "role": "system",
        "content": JARVIS_SYSTEM_PROMPT
    },    
]

def get_jarvis_response(user_input: str, tool_results: str) -> dict:
    messages.append({
        "role":"user",
        "content":JARVIS_USER_PROMPT.format(user_query=user_input, tool_results=tool_results)
    })
    # response = completion(
    #     model="openrouter/nvidia/nemotron-3.5-lightning:free",
    #     messages=messages,
    #     api_key=os.environ["OPENROUTER_API_KEY"],
    #     max_completion_tokens=20,
    #     reasoning_effort="none",
    #     max_tokens=1024
    # )
    
    # Inside a container "localhost" is the container itself, so the Ollama
    # endpoint is configurable. Point OLLAMA_API_BASE at the host (or at the
    # ollama service in docker-compose.yml) when running containerized.
    response = completion(
        model=os.getenv("OLLAMA_MODEL", "ollama/gemma4:e2b"),
        api_base=os.getenv("OLLAMA_API_BASE", "http://localhost:11434"),
        messages=messages,
        max_completion_tokens=20,
        reasoning_effort="none",
        max_tokens=1024
    )    
    
    llm_response = response.choices[0].message
    
    messages.append({
        "role":"assistant",
        "content":llm_response.content
    })

    print("[LLM] ", llm_response.content)
    return llm_response