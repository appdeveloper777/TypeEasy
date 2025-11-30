#!/usr/bin/env python3
import sys

# Read the original file
with open('src/servidor_agent.c', 'r', encoding='utf-8') as f:
    lines = f.readlines()

# Find the line where handle_chat_bridge ends (line with single "}")
insert_line = None
for i, line in enumerate(lines):
    if i > 110 and i < 130 and line.strip() == '}' and 'handle_chat_bridge' in ''.join(lines[max(0,i-20):i]):
        insert_line = i
        break

if insert_line is None:
    print("Could not find insertion point")
    sys.exit(1)

# Insert the new code before the closing brace
new_code = '''    else if (strcmp(method_name, "post") == 0 && args != NULL) {
        // Chat.post("/send", message) - enviar a WAHA
        char* path = NULL;
        char* message = NULL;
        
        path = get_node_string(args);
        if (args->right) {
            message = get_node_string(args->right);
        }
        
        if (!path) path = strdup("");
        if (!message) message = strdup("");
        
        te_log("Chat.post called. Path: %s", path);
        
        // TODO: Implement sending to WAHA
        // For now, just log it
        te_log("Would send to WAHA: %s", message);
        
        free(path);
        free(message);
    }
'''

# Insert before the closing brace
lines.insert(insert_line, new_code)

# Write back
with open('src/servidor_agent.c', 'w', encoding='utf-8') as f:
    f.writelines(lines)

print(f"Successfully inserted code at line {insert_line}")
