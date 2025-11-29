import protobuf from 'protobufjs';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const projectRoot = path.resolve(__dirname, '../..');

// Function to convert proto field type to our schema format
function getFieldType(field) {
  const typeMap = {
    'double': 'double',
    'float': 'float',
    'int32': 'int32',
    'int64': 'int64',
    'uint32': 'uint32',
    'uint64': 'uint64',
    'sint32': 'int32',
    'sint64': 'int64',
    'fixed32': 'uint32',
    'fixed64': 'uint64',
    'sfixed32': 'int32',
    'sfixed64': 'int64',
    'bool': 'bool',
    'string': 'string',
    'bytes': 'bytes',
  };
  
  if (field.type === 'enum' && field.resolvedType) {
    return { type: 'enum', enumType: field.resolvedType.name };
  }
  if (field.type === 'message' && field.resolvedType) {
    return { type: 'message', messageType: field.resolvedType.name };
  }
  return typeMap[field.type] || field.type;
}

// Recursively extract message schema
function extractMessageSchema(messageType, root) {
  const schema = {
    name: messageType.name,
    fullName: messageType.fullName,
    fields: [],
    oneofs: [],
  };

  // Extract fields
  messageType.fieldsArray.forEach(field => {
    // Skip fields that are part of a oneof (they'll be handled separately)
    if (field.partOfOneof) {
      return;
    }

    const fieldSchema = {
      name: field.name,
      number: field.id,
      type: getFieldType(field),
      repeated: field.repeated,
      optional: field.optional,
    };

    // If it's a message type, recursively extract its schema
    if (field.resolvedType && field.resolvedType instanceof protobuf.Type) {
      fieldSchema.messageSchema = extractMessageSchema(field.resolvedType, root);
    }

    // If it's an enum, extract enum values
    if (field.resolvedType && field.resolvedType instanceof protobuf.Enum) {
      const enumValues = [];
      const enumObj = field.resolvedType.values;
      for (const key in enumObj) {
        if (enumObj.hasOwnProperty(key) && typeof enumObj[key] === 'number') {
          enumValues.push({
            name: key,
            number: enumObj[key],
          });
        }
      }
      // Sort by number
      enumValues.sort((a, b) => a.number - b.number);
      fieldSchema.enumValues = enumValues;
    }

    schema.fields.push(fieldSchema);
  });

  // Extract oneofs
  messageType.oneofsArray.forEach(oneof => {
    const oneofSchema = {
      name: oneof.name,
      fields: [],
    };

    oneof.fieldsArray.forEach(field => {
      const fieldSchema = {
        name: field.name,
        number: field.id,
        type: getFieldType(field),
      };

      if (field.resolvedType && field.resolvedType instanceof protobuf.Type) {
        fieldSchema.messageSchema = extractMessageSchema(field.resolvedType, root);
      }

      if (field.resolvedType && field.resolvedType instanceof protobuf.Enum) {
        const enumValues = [];
        const enumObj = field.resolvedType.values;
        for (const key in enumObj) {
          if (enumObj.hasOwnProperty(key) && typeof enumObj[key] === 'number') {
            enumValues.push({
              name: key,
              number: enumObj[key],
            });
          }
        }
        // Sort by number
        enumValues.sort((a, b) => a.number - b.number);
        fieldSchema.enumValues = enumValues;
      }

      oneofSchema.fields.push(fieldSchema);
    });

    schema.oneofs.push(oneofSchema);
  });

  return schema;
}

// Extract enum schema
function extractEnumSchema(enumType) {
  const values = [];
  const enumObj = enumType.values;
  for (const key in enumObj) {
    if (enumObj.hasOwnProperty(key) && typeof enumObj[key] === 'number') {
      values.push({
        name: key,
        number: enumObj[key],
      });
    }
  }
  // Sort by number
  values.sort((a, b) => a.number - b.number);
  return {
    name: enumType.name,
    fullName: enumType.fullName,
    values: values,
  };
}

// Recursively find all .proto files in a directory
function findProtoFiles(dir, fileList = []) {
  try {
    // Skip special filesystems entirely
    const dirPath = path.resolve(dir);
    if (dirPath.startsWith('/proc/') || 
        dirPath.startsWith('/sys/') || 
        dirPath.startsWith('/dev/') ||
        dirPath === '/proc' ||
        dirPath === '/sys' ||
        dirPath === '/dev') {
      return fileList;
    }
    
    const files = fs.readdirSync(dir);
    
    files.forEach(file => {
      try {
        const filePath = path.join(dir, file);
        const resolvedPath = path.resolve(filePath);
        
        // Skip special filesystems
        if (resolvedPath.startsWith('/proc/') || 
            resolvedPath.startsWith('/sys/') || 
            resolvedPath.startsWith('/dev/')) {
          return;
        }
        
        const stat = fs.statSync(filePath);
        
        if (stat.isDirectory()) {
          // Skip node_modules, bazel-*, and other build directories
          if (!file.startsWith('.') && 
              !file.startsWith('bazel') && 
              file !== 'node_modules' &&
              file !== 'dist' &&
              file !== 'build') {
            findProtoFiles(filePath, fileList);
          }
        } else if (file.endsWith('.proto')) {
          fileList.push(filePath);
        }
      } catch (err) {
        // Silently skip expected errors (file descriptors, symlinks, permissions, etc.)
        // Only log unexpected errors
        if (err.code !== 'ENOENT' && 
            err.code !== 'EACCES' && 
            err.code !== 'ELOOP' &&
            err.code !== 'ENOTDIR') {
          // Only warn for truly unexpected errors
          console.warn(`Warning: Could not access ${path.join(dir, file)}: ${err.message}`);
        }
      }
    });
  } catch (err) {
    // Silently skip expected errors
    if (err.code !== 'ENOENT' && 
        err.code !== 'EACCES' && 
        err.code !== 'ELOOP' &&
        err.code !== 'ENOTDIR') {
      console.warn(`Warning: Could not read directory ${dir}: ${err.message}`);
    }
  }
  
  return fileList;
}

async function generateSchema() {
  const root = new protobuf.Root();
  
  // Resolve import paths relative to project root
  root.resolvePath = (origin, target) => {
    // Handle paths that start with known prefixes
    if (target.startsWith('config/')) {
      return path.join(projectRoot, target);
    }
    if (target.startsWith('robot/')) {
      return path.join(projectRoot, target);
    }
    if (target.startsWith('ai/')) {
      return path.join(projectRoot, target);
    }
    // Try relative to project root
    const projectRelativePath = path.join(projectRoot, target);
    if (fs.existsSync(projectRelativePath)) {
      return projectRelativePath;
    }
    // Fall back to default resolution
    return protobuf.util.path.resolve(origin, target);
  };

  // Dynamically find all proto files in the project
  const protoFiles = findProtoFiles(projectRoot);
  
  if (protoFiles.length === 0) {
    throw new Error('No proto files found in project');
  }
  
  console.log(`Found ${protoFiles.length} proto files:`);
  protoFiles.forEach(p => console.log(`  - ${path.relative(projectRoot, p)}`));

  // Load all proto files to ensure all types are available
  // Load them in dependency order by starting with the main config file
  const mainConfigPath = path.join(projectRoot, 'config/proto/config.proto');
  const otherProtoFiles = protoFiles.filter(p => p !== mainConfigPath);
  
  let loadedCount = 0;
  let failedCount = 0;
  
  // Load main config first (it will pull in dependencies via imports)
  if (fs.existsSync(mainConfigPath)) {
    try {
      await root.load(mainConfigPath, { keepCase: true });
      loadedCount++;
      console.log(`✓ Loaded main config: ${path.relative(projectRoot, mainConfigPath)}`);
    } catch (error) {
      console.error(`✗ Failed to load main config: ${error.message}`);
      throw error;
    }
  } else {
    console.warn(`Warning: Main config file not found: ${path.relative(projectRoot, mainConfigPath)}`);
  }
  
  // Load remaining proto files to ensure all types are captured
  // protobufjs will skip already loaded files, so this is safe
  for (const protoFile of otherProtoFiles) {
    try {
      await root.load(protoFile, { keepCase: true });
      loadedCount++;
    } catch (error) {
      // Some files might fail to load if they're missing dependencies
      // that are only available in certain build contexts, but that's okay
      // as long as the main config and its dependencies load successfully
      failedCount++;
      console.warn(`⚠ Could not load ${path.relative(projectRoot, protoFile)}: ${error.message}`);
    }
  }
  
  console.log(`Loaded ${loadedCount} proto files${failedCount > 0 ? ` (${failedCount} failed)` : ''}`);

  // Find the Config message
  const ConfigType = root.lookupType('config.Config');
  if (!ConfigType) {
    throw new Error('Could not find config.Config message type');
  }

  // Extract all message and enum types from the entire root
  const messages = {};
  const enums = {};

  function collectTypes(namespace) {
    if (!namespace || !namespace.nestedArray) {
      return;
    }
    
    namespace.nestedArray.forEach(nested => {
      if (nested instanceof protobuf.Type) {
        // Only add if not already present (avoid duplicates)
        if (!messages[nested.fullName]) {
          messages[nested.fullName] = extractMessageSchema(nested, root);
        }
        // Recursively collect nested types
        collectTypes(nested);
      } else if (nested instanceof protobuf.Enum) {
        // Only add if not already present (avoid duplicates)
        if (!enums[nested.fullName]) {
          enums[nested.fullName] = extractEnumSchema(nested);
        }
      } else if (nested instanceof protobuf.Namespace) {
        // Recursively collect from nested namespaces
        collectTypes(nested);
      }
    });
  }

  // Collect all types from the root and all nested namespaces
  collectTypes(root);

  // Find the actual key for the Config message
  // Try exact match first, then look for any Config message in config package
  let configKey = 'config.Config';
  const exactMatch = messages['config.Config'];
  if (exactMatch) {
    configKey = 'config.Config';
  } else {
    const configMatch = Object.keys(messages).find(k => 
      k.endsWith('.Config') && k.includes('config')
    );
    if (configMatch) {
      configKey = configMatch;
    } else {
      console.warn(`Warning: Could not find config.Config, using first available: ${Object.keys(messages)[0] || 'none'}`);
      configKey = Object.keys(messages)[0] || 'config.Config';
    }
  }
  
  const schema = {
    rootMessage: configKey,
    messages,
    enums,
  };

  // Write schema to file in public directory so it's served as a static asset
  // Append timestamp to filename for history, and also write non-timestamped version for app use
  const timestamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, -5); // Format: 2024-01-15T10-30-45
  const timestampedPath = path.join(__dirname, `../public/proto-schema-${timestamp}.json`);
  const outputPath = path.join(__dirname, '../public/proto-schema.json');
  const schemaJson = JSON.stringify(schema, null, 2);
  
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  
  // Write timestamped version for history
  fs.writeFileSync(timestampedPath, schemaJson);
  
  // Write non-timestamped version for app to use
  fs.writeFileSync(outputPath, schemaJson);

  console.log(`✓ Generated proto schema: ${outputPath}`);
  console.log(`  (Also saved as: proto-schema-${timestamp}.json)`);
  console.log(`  - ${Object.keys(messages).length} message types`);
  console.log(`  - ${Object.keys(enums).length} enum types`);
  console.log(`  - Root message: ${configKey}`);
}

generateSchema().catch(console.error);

