// Simple parser for .pbtxt (text format protobuf) files
// This is a simplified parser that handles the basic structure

export interface PbtxtValue {
  [key: string]: any;
}

export function parsePbtxt(content: string): PbtxtValue {
  const lines = content.split('\n');
  let i = 0;

  function parseValue(value: string): any {
    value = value.trim();
    
    // String (with or without quotes)
    if (value.startsWith('"') && value.endsWith('"')) {
      return value.slice(1, -1);
    }
    
    // Boolean
    if (value === 'true') return true;
    if (value === 'false') return false;
    
    // Number
    if (/^-?\d+$/.test(value)) {
      const num = parseInt(value, 10);
      // Check if it's actually a uint64/int64 that needs to be a string
      if (Math.abs(num) > Number.MAX_SAFE_INTEGER) {
        return value; // Keep as string for large integers
      }
      return num;
    }
    
    if (/^-?\d+\.\d+$/.test(value)) {
      return parseFloat(value);
    }
    
    // Enum value (unquoted identifier)
    if (/^[A-Z_][A-Z0-9_]*$/.test(value)) {
      return value;
    }
    
    return value;
  }

  function parseBlock(indent: number = 0): { value: PbtxtValue; consumed: number } {
    const block: PbtxtValue = {};
    let consumed = 0;
    
    while (i < lines.length) {
      const line = lines[i].trim();
      if (!line || line.startsWith('#')) {
        i++;
        consumed++;
        continue;
      }
      
      const currentIndent = lines[i].length - lines[i].trimStart().length;
      if (currentIndent < indent) {
        break;
      }
      
      // Check if this line starts a nested block
      const match = line.match(/^(\w+)\s*\{/);
      if (match) {
        const fieldName = match[1];
        i++;
        consumed++;
        const { value: nestedValue, consumed: nestedConsumed } = parseBlock(currentIndent + 2);
        block[fieldName] = nestedValue;
        i += nestedConsumed;
        consumed += nestedConsumed;
        continue;
      }
      
      // Check if this line is a field assignment
      const fieldMatch = line.match(/^(\w+)\s*:\s*(.+)$/);
      if (fieldMatch) {
        const [, fieldName, value] = fieldMatch;
        const parsedValue = parseValue(value);
        
        // Handle repeated fields (array)
        if (block[fieldName]) {
          if (!Array.isArray(block[fieldName])) {
            block[fieldName] = [block[fieldName]];
          }
          block[fieldName].push(parsedValue);
        } else {
          block[fieldName] = parsedValue;
        }
        
        i++;
        consumed++;
        continue;
      }
      
      // Check if this line closes a block
      if (line === '}') {
        i++;
        consumed++;
        break;
      }
      
      i++;
      consumed++;
    }
    
    return { value: block, consumed };
  }

  const { value } = parseBlock();
  return value;
}

export function formatPbtxt(obj: PbtxtValue, indent: number = 0): string {
  const indentStr = ' '.repeat(indent);
  const lines: string[] = [];
  
  for (const [key, value] of Object.entries(obj)) {
    if (value === null || value === undefined) {
      continue;
    }
    
    if (Array.isArray(value)) {
      for (const item of value) {
        if (typeof item === 'object' && item !== null) {
          lines.push(`${indentStr}${key} {`);
          lines.push(formatPbtxt(item, indent + 2));
          lines.push(`${indentStr}}`);
        } else {
          lines.push(`${indentStr}${key}: ${formatValue(item)}`);
        }
      }
    } else if (typeof value === 'object' && value !== null) {
      lines.push(`${indentStr}${key} {`);
      lines.push(formatPbtxt(value, indent + 2));
      lines.push(`${indentStr}}`);
    } else {
      lines.push(`${indentStr}${key}: ${formatValue(value)}`);
    }
  }
  
  return lines.join('\n');
}

function formatValue(value: any): string {
  if (typeof value === 'string') {
    // Check if it's an enum value (all caps with underscores)
    if (/^[A-Z_][A-Z0-9_]*$/.test(value)) {
      return value;
    }
    return `"${value}"`;
  }
  if (typeof value === 'boolean') {
    return value ? 'true' : 'false';
  }
  return String(value);
}

