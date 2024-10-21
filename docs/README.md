<img src="https://github.com/user-attachments/assets/46a5c546-7e9b-42c7-87f4-bc8defe674e0" width=250 />

# DuckDB Open Prompt Extension
This very experimental extension to query OpenAI compatible API endpoints such as Ollama

> Experimental: USE AT YOUR OWN RISK!

### Functions
- `open_prompt()`

### Settings
```sql
SELECT set_api_token('your_api_key_here');
SELECT set_api_url('http://localhost:11434/v1/chat/completions');
```

### Usage
```
D SELECT open_prompt('Write a one-line poem about ducks', 'qwen2.5:0.5b') AS response;
┌────────────────────────────────────────────────┐
│                    response                    │
│                    varchar                     │
├────────────────────────────────────────────────┤
│ Ducks quacking at dawn, swimming in the light. │
└────────────────────────────────────────────────┘
```
