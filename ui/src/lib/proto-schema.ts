// Type definitions for the generated proto schema
export interface ProtoSchema {
  rootMessage: string;
  messages: Record<string, MessageSchema>;
  enums: Record<string, EnumSchema>;
}

export interface MessageSchema {
  name: string;
  fullName: string;
  fields: FieldSchema[];
  oneofs: OneofSchema[];
}

export interface FieldSchema {
  name: string;
  number: number;
  type: string | { type: 'enum'; enumType: string } | { type: 'message'; messageType: string };
  repeated?: boolean;
  optional?: boolean;
  messageSchema?: MessageSchema;
  enumValues?: EnumValue[];
}

export interface EnumSchema {
  name: string;
  fullName: string;
  values: EnumValue[];
}

export interface EnumValue {
  name: string;
  number: number;
}

export interface OneofSchema {
  name: string;
  fields: FieldSchema[];
}

// Load the generated schema
let schemaCache: ProtoSchema | null = null;

export async function loadSchema(): Promise<ProtoSchema> {
  if (schemaCache) {
    return schemaCache;
  }
  
  // Load from public directory as a static asset
  // This avoids Vite trying to parse it as a module
  try {
    const response = await fetch('/proto-schema.json');
    if (!response.ok) {
      throw new Error(`Failed to load schema: ${response.statusText}`);
    }
    schemaCache = await response.json() as ProtoSchema;
    return schemaCache;
  } catch (error) {
    console.error('Failed to load schema:', error);
    throw error;
  }
}

// Helper to get a pretty label from a field name
export function prettyLabelForField(fieldName: string): string {
  return fieldName
    .split('_')
    .map(word => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');
}

