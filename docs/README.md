<img src="https://github.com/user-attachments/assets/46a5c546-7e9b-42c7-87f4-bc8defe674e0" width=250 />

# DuckDB Open Prompt Extension
Simple extension to query OpenAI Completion API endpoints such as Ollama

> Experimental: USE AT YOUR OWN RISK!

### Functions
- `open_prompt(prompt, model)`
- `set_api_token(auth_token)`
- `set_api_url(completions_url)`
- `set_model_name(model_name)`
- `set_json_schema(json_schema)`

### Settings
Setup the completions API configuration w/ optional auth token and model name
```sql
SET VARIABLE openprompt_api_url = 'http://localhost:11434/v1/chat/completions';
SET VARIABLE openprompt_api_token = 'optional_api_key_here';
SET VARIABLE openprompt_model_name = 'qwen2.5:0.5b';

```

### Usage
```sql
D SELECT open_prompt('Write a one-line poem about ducks') AS response;
┌────────────────────────────────────────────────┐
│                    response                    │
│                    varchar                     │
├────────────────────────────────────────────────┤
│ Ducks quacking at dawn, swimming in the light. │
└────────────────────────────────────────────────┘
```

#### JSON Structured Output _(very experimental)_
Define a `json_schema` to receive a structured response in JSON format... most of the time. Prompt based. Output depends on model skills.

```sql
D SET VARIABLE openprompt_json_schema = "struct:={summary: 'VARCHAR', favourite_animals:='VARCHAR[]', favourite_activity:='VARCHAR[]', star_rating:='INTEGER'}, struct_descr:={star_rating: 'visit rating on a scale from 1 (bad) to 5 (very good)'}";
D SELECT open_prompt('My zoo visit was fun and I loved the bears and tigets. i also had icecream') AS response;
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                        response                                                                         │
│                                                                         varchar                                                                         │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ {"summary": "A short summary of your recent zoo visit activity.", "favourite_animals": ["bears", "tigers"], "favourite_activity": ["icecream"], "sta …  │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

```sql
D LOAD json;                                      ^
D WITH response AS (SELECT open_prompt('My zoo visit was fun and I loved the bears and tigets. i also had icecream') AS response) SELECT json_structure(response) FROM response;
┌────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                          json_structure(response)                                          │
│                                                    json                                                    │
├────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ {"summary":"VARCHAR","favourite_animals":"VARCHAR","favourite_activity":"VARCHAR","star_rating":"UBIGINT"} │
└────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

<br>

<img src="https://github.com/user-attachments/assets/824bfab2-aca6-4bd9-8a4a-bc01901fcd5b" width=100 />

### Ollama self-hosted
Test the open_prompt extension using a local or remote Ollama with Completions API

#### CPU only
```
docker run -d -v ollama:/root/.ollama -p 11434:11434 --name ollama ollama/ollama
```
#### Nvidia GPU
Install the Nvidia container toolkit. Run Ollama inside a Docker container
```
docker run -d --gpus=all -v ollama:/root/.ollama -p 11434:11434 --name ollama ollama/ollama
```
