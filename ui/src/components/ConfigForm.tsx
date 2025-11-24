import { useState, useCallback } from 'react'
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import { Label } from '@/components/ui/label'
import { Input } from '@/components/ui/input'
import { Button } from '@/components/ui/button'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import { Plus, Trash2 } from 'lucide-react'
import type { MessageSchema, FieldSchema, ProtoSchema } from '@/lib/proto-schema'
import { prettyLabelForField } from '@/lib/proto-schema'

interface ConfigFormProps {
  schema: ProtoSchema
  value: any
  onChange: (value: any) => void
}

interface MessageFormProps {
  messageSchema: MessageSchema
  value: any
  onChange: (value: any) => void
  title?: string
}

interface FieldEditorProps {
  field: FieldSchema
  value: any
  onChange: (value: any) => void
  enumValues?: Array<{ name: string; number: number }>
  messageSchema?: MessageSchema
}

function FieldEditor({ field, value, onChange, enumValues, messageSchema }: FieldEditorProps) {
  const isRepeated = field.repeated
  const fieldValue = value?.[field.name]

  if (isRepeated) {
    const items = Array.isArray(fieldValue) ? fieldValue : []
    
    return (
      <div className="space-y-2">
        <div className="flex items-center justify-between">
          <Label>{prettyLabelForField(field.name)}</Label>
          <Button
            type="button"
            variant="outline"
            size="sm"
            onClick={() => {
              const newItems = [...items, getDefaultValue(field, enumValues, messageSchema)]
              onChange({ ...value, [field.name]: newItems })
            }}
          >
            <Plus className="h-4 w-4 mr-1" />
            Add
          </Button>
        </div>
        {items.map((item: any, index: number) => (
          <div key={index} className="flex gap-2 items-center">
            <div className="flex-1">
              {messageSchema ? (
                <MessageForm
                  messageSchema={messageSchema}
                  value={item}
                  onChange={(newItem) => {
                    const newItems = [...items]
                    newItems[index] = newItem
                    onChange({ ...value, [field.name]: newItems })
                  }}
                />
              ) : (
                <ScalarFieldEditor
                  field={field}
                  value={item}
                  onChange={(newValue) => {
                    const newItems = [...items]
                    newItems[index] = newValue
                    onChange({ ...value, [field.name]: newItems })
                  }}
                  enumValues={enumValues}
                />
              )}
            </div>
            <Button
              type="button"
              variant="ghost"
              size="icon"
              onClick={() => {
                const newItems = items.filter((_: any, i: number) => i !== index)
                onChange({ ...value, [field.name]: newItems })
              }}
            >
              <Trash2 className="h-4 w-4" />
            </Button>
          </div>
        ))}
      </div>
    )
  }

  if (messageSchema) {
    return (
      <div className="space-y-2">
        <Label>{prettyLabelForField(field.name)}</Label>
        <MessageForm
          messageSchema={messageSchema}
          value={fieldValue || {}}
          onChange={(newValue) => onChange({ ...value, [field.name]: newValue })}
        />
      </div>
    )
  }

  return (
    <div className="space-y-2">
      <Label>{prettyLabelForField(field.name)}</Label>
      <ScalarFieldEditor
        field={field}
        value={fieldValue}
        onChange={(newValue) => onChange({ ...value, [field.name]: newValue })}
        enumValues={enumValues}
      />
    </div>
  )
}

function ScalarFieldEditor({
  field,
  value,
  onChange,
  enumValues,
}: {
  field: FieldSchema
  value: any
  onChange: (value: any) => void
  enumValues?: Array<{ name: string; number: number }>
}) {
  const fieldType = typeof field.type === 'string' ? field.type : field.type.type

  if (fieldType === 'enum' || enumValues) {
    const enumType = typeof field.type === 'object' && 'enumType' in field.type
      ? field.type.enumType
      : null
    const options = enumValues || []
    const UNSET_VALUE = '__unset__'

    return (
      <Select
        value={value || UNSET_VALUE}
        onValueChange={(newValue) => {
          onChange(newValue === UNSET_VALUE ? undefined : newValue)
        }}
      >
        <SelectTrigger>
          <SelectValue placeholder="<unset>" />
        </SelectTrigger>
        <SelectContent>
          <SelectItem value={UNSET_VALUE}>&lt;unset&gt;</SelectItem>
          {options.map((opt) => (
            <SelectItem key={opt.name} value={opt.name}>
              {opt.name}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    )
  }

  switch (fieldType) {
    case 'bool':
      return (
        <div className="flex items-center space-x-2">
          <input
            type="checkbox"
            checked={value || false}
            onChange={(e) => onChange(e.target.checked)}
            className="h-4 w-4 rounded border-gray-300"
          />
        </div>
      )
    case 'string':
      return (
        <Input
          type="text"
          value={value || ''}
          onChange={(e) => onChange(e.target.value)}
        />
      )
    case 'int32':
    case 'uint32':
      return (
        <Input
          type="number"
          value={value || 0}
          onChange={(e) => onChange(parseInt(e.target.value, 10) || 0)}
        />
      )
    case 'int64':
    case 'uint64':
      return (
        <Input
          type="text"
          value={value || ''}
          onChange={(e) => onChange(e.target.value)}
          placeholder="64-bit integer"
        />
      )
    case 'float':
    case 'double':
      return (
        <Input
          type="number"
          step="any"
          value={value || 0}
          onChange={(e) => onChange(parseFloat(e.target.value) || 0)}
        />
      )
    default:
      return (
        <Input
          type="text"
          value={value || ''}
          onChange={(e) => onChange(e.target.value)}
        />
      )
  }
}

function getDefaultValue(
  field: FieldSchema,
  enumValues?: Array<{ name: string; number: number }>,
  messageSchema?: MessageSchema
): any {
  if (messageSchema) {
    return {}
  }

  const fieldType = typeof field.type === 'string' ? field.type : field.type.type

  switch (fieldType) {
    case 'bool':
      return false
    case 'string':
      return ''
    case 'int32':
    case 'uint32':
    case 'int64':
    case 'uint64':
      return 0
    case 'float':
    case 'double':
      return 0.0
    case 'enum':
      return enumValues?.[0]?.name || ''
    default:
      return ''
  }
}

function MessageForm({ messageSchema, value, onChange, title }: MessageFormProps) {
  const currentValue = value || {}

  return (
    <Card>
      {title && (
        <CardHeader>
          <CardTitle>{title}</CardTitle>
        </CardHeader>
      )}
      <CardContent className="space-y-4 pt-6">
        {messageSchema.fields.map((field) => {
          const fieldType = typeof field.type === 'string' ? field.type : field.type.type
          const isMessage = fieldType === 'message' || field.messageSchema
          const isEnum = fieldType === 'enum' || field.enumValues

          return (
            <FieldEditor
              key={field.name}
              field={field}
              value={currentValue}
              onChange={onChange}
              enumValues={field.enumValues}
              messageSchema={field.messageSchema}
            />
          )
        })}

        {messageSchema.oneofs.map((oneof) => {
          // Find which field in the oneof is set
          const setField = oneof.fields.find((f) => currentValue[f.name] !== undefined)
          const selectedIndex = setField ? oneof.fields.indexOf(setField) : -1
          const NONE_VALUE = '__none__'

          return (
            <div key={oneof.name} className="space-y-2">
              <Label>{prettyLabelForField(oneof.name)}</Label>
              <Select
                value={selectedIndex >= 0 ? String(selectedIndex) : NONE_VALUE}
                onValueChange={(newIndex) => {
                  const newValue = { ...currentValue }
                  // Clear all oneof fields
                  oneof.fields.forEach((f) => {
                    delete newValue[f.name]
                  })
                  // Set the selected field
                  if (newIndex && newIndex !== NONE_VALUE && parseInt(newIndex, 10) >= 0) {
                    const selectedField = oneof.fields[parseInt(newIndex, 10)]
                    if (selectedField) {
                      newValue[selectedField.name] = selectedField.messageSchema ? {} : getDefaultValue(selectedField, selectedField.enumValues, selectedField.messageSchema)
                    }
                  }
                  onChange(newValue)
                }}
              >
                <SelectTrigger>
                  <SelectValue placeholder="<none>" />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value={NONE_VALUE}>&lt;none&gt;</SelectItem>
                  {oneof.fields.map((field, index) => (
                    <SelectItem key={field.name} value={String(index)}>
                      {prettyLabelForField(field.name)}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
              {selectedIndex >= 0 && oneof.fields[selectedIndex] && (
                <div className="ml-4">
                  <FieldEditor
                    field={oneof.fields[selectedIndex]}
                    value={currentValue}
                    onChange={onChange}
                    enumValues={oneof.fields[selectedIndex].enumValues}
                    messageSchema={oneof.fields[selectedIndex].messageSchema}
                  />
                </div>
              )}
            </div>
          )
        })}
      </CardContent>
    </Card>
  )
}

export function ConfigForm({ schema, value, onChange }: ConfigFormProps) {
  // Try both with and without leading dot
  const rootMessage = schema.messages[schema.rootMessage] || 
                      schema.messages[`.${schema.rootMessage}`] ||
                      schema.messages[Object.keys(schema.messages).find(k => k.endsWith(`.${schema.rootMessage.split('.').pop()}`)) || '']
  if (!rootMessage) {
    return <div>Error: Root message not found: {schema.rootMessage}</div>
  }

  return (
    <MessageForm
      messageSchema={rootMessage}
      value={value || {}}
      onChange={onChange}
      title="Config"
    />
  )
}

