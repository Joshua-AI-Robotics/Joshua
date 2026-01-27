import { useState, useMemo, useRef, useEffect } from 'react'
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card'
import { Input } from './ui/input'
import { useZenohSSE, type ZenohEvent } from '@/hooks/useZenohSSE'
import { Search, X, ChevronDown } from 'lucide-react'

function formatTimestamp(timestamp: number): { date: string; time: string; full: string } {
  const date = new Date(timestamp)
  const dateStr = date.toISOString().slice(0, 10)
  const timeStr = date.toISOString().slice(11, 23)
  return {
    date: dateStr,
    time: timeStr,
    full: `${dateStr} ${timeStr}`,
  }
}

function formatLogLevel(value: any): { level: string; color: string } {
  if (typeof value === 'object' && value !== null) {
    // Check for common log level fields
    if (value.level) {
      const level = String(value.level).toLowerCase()
      if (level.includes('error') || level.includes('fatal')) {
        return { level: 'ERROR', color: 'text-red-600 dark:text-red-400' }
      }
      if (level.includes('warn')) {
        return { level: 'WARN', color: 'text-yellow-600 dark:text-yellow-400' }
      }
      if (level.includes('info')) {
        return { level: 'INFO', color: 'text-blue-600 dark:text-blue-400' }
      }
      if (level.includes('debug')) {
        return { level: 'DEBUG', color: 'text-gray-600 dark:text-gray-400' }
      }
    }
  }
  return { level: 'LOG', color: 'text-muted-foreground' }
}

interface LogEntry {
  timestamp: number
  key: string
  value: any
  formattedTime: { date: string; time: string; full: string }
  level: { level: string; color: string }
  message: string
  source: string
}

// Multi-select dropdown component
function MultiSelect({
  options,
  selected,
  onSelectionChange,
  placeholder,
  className,
}: {
  options: string[]
  selected: Set<string>
  onSelectionChange: (selected: Set<string>) => void
  placeholder: string
  className?: string
}) {
  const [isOpen, setIsOpen] = useState(false)
  const dropdownRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    const handleClickOutside = (event: MouseEvent) => {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target as Node)) {
        setIsOpen(false)
      }
    }
    if (isOpen) {
      document.addEventListener('mousedown', handleClickOutside)
      return () => document.removeEventListener('mousedown', handleClickOutside)
    }
  }, [isOpen])

  const toggleOption = (option: string) => {
    const newSelected = new Set(selected)
    if (newSelected.has(option)) {
      newSelected.delete(option)
    } else {
      newSelected.add(option)
    }
    onSelectionChange(newSelected)
  }

  const displayText = selected.size === 0 
    ? placeholder 
    : selected.size === 1 
    ? Array.from(selected)[0]
    : `${selected.size} selected`

  return (
    <div className={`relative ${className}`} ref={dropdownRef}>
      <button
        type="button"
        onClick={() => setIsOpen(!isOpen)}
        className="flex h-7 w-full items-center justify-between rounded-md border border-input bg-background px-2 text-xs ring-offset-background focus:outline-none focus:ring-2 focus:ring-ring focus:ring-offset-1"
      >
        <span className="truncate">{displayText}</span>
        <ChevronDown className="h-3 w-3 opacity-50 shrink-0 ml-1" />
      </button>
      {isOpen && (
        <div className="absolute z-[100] mt-1 w-full rounded-md border bg-popover shadow-md max-h-60 overflow-auto">
          <div className="p-1">
            {options.length === 0 ? (
              <div className="px-2 py-1.5 text-xs text-muted-foreground">No options</div>
            ) : (
              options.map((option) => (
                <label
                  key={option}
                  className="flex items-center px-2 py-1.5 text-xs cursor-pointer hover:bg-accent rounded-sm"
                >
                  <input
                    type="checkbox"
                    checked={selected.has(option)}
                    onChange={() => toggleOption(option)}
                    className="mr-2 h-3 w-3 rounded border-gray-300"
                  />
                  <span className="flex-1 truncate">{option}</span>
                </label>
              ))
            )}
          </div>
        </div>
      )}
    </div>
  )
}

export default function LogViewer() {
  // Poll logs more frequently (every 500ms)
  const { events } = useZenohSSE('@/*/ros2/**', { pollInterval: 500 })
  const [searchQuery, setSearchQuery] = useState('')
  const [filterLevel, setFilterLevel] = useState<Set<string>>(new Set())
  const [filterSource, setFilterSource] = useState<Set<string>>(new Set())
  const [filterMessage, setFilterMessage] = useState<Set<string>>(new Set())
  const maxEntries = 1000 // Maximum number of log entries to display
  const scrollRef = useRef<HTMLDivElement>(null)
  const [autoScroll, setAutoScroll] = useState(true)
  const prevEventsLengthRef = useRef(0)
  const newEventIdsRef = useRef<Set<string>>(new Set()) // Track newly added events

  const logEntriesWithFilters = useMemo(() => {
    const entries: LogEntry[] = events.map((event: ZenohEvent) => {
      const value = event.value || {}
      const key = typeof event.key === 'string' ? event.key : JSON.stringify(event.key)
      
      // Extract source from key (e.g., node name or topic)
      const sourceMatch = key.match(/\/([^/]+)$/)
      const source = sourceMatch ? sourceMatch[1] : key.split('/').pop() || 'unknown'
      
      // Format message as one-liner
      let message = ''
      if (typeof value === 'string') {
        message = value.replace(/\n/g, ' ').replace(/\s+/g, ' ').trim()
      } else if (value.message || value.msg) {
        message = String(value.message || value.msg).replace(/\n/g, ' ').replace(/\s+/g, ' ').trim()
      } else if (value.ros2_name) {
        message = `Topic: ${value.ros2_name} (${value.ros2_type || 'unknown'})`
      } else if (Object.keys(value).length > 0) {
        // Create a one-line summary
        const summary = Object.entries(value)
          .filter(([k]) => !['key', 'encoding', 'timestamp'].includes(k))
          .map(([k, v]) => {
            if (typeof v === 'object' && v !== null) {
              return `${k}: ${JSON.stringify(v).replace(/\n/g, ' ').replace(/\s+/g, ' ').slice(0, 150)}`
            }
            return `${k}: ${String(v).replace(/\n/g, ' ').replace(/\s+/g, ' ').slice(0, 150)}`
          })
          .join(', ')
        message = summary || JSON.stringify(value).replace(/\n/g, ' ').replace(/\s+/g, ' ').trim()
      } else {
        message = JSON.stringify(value).replace(/\n/g, ' ').replace(/\s+/g, ' ').trim()
      }
      
      const level = formatLogLevel(value)

      return {
        timestamp: event.timestamp,
        key,
        value,
        formattedTime: formatTimestamp(event.timestamp),
        level,
        message: String(message),
        source,
      }
    })

    // Sort by timestamp (newest first)
    entries.sort((a, b) => b.timestamp - a.timestamp)

    // Extract unique values for filters
    const uniqueLevels = new Set<string>()
    const uniqueSources = new Set<string>()
    
    entries.forEach((entry) => {
      uniqueLevels.add(entry.level.level)
      uniqueSources.add(entry.source)
    })

    // Filter by search query and column filters
    const filtered = entries.filter((entry) => {
      // Global search query
      if (searchQuery) {
        const matchesSearch =
          entry.key.toLowerCase().includes(searchQuery.toLowerCase()) ||
          entry.message.toLowerCase().includes(searchQuery.toLowerCase()) ||
          entry.source.toLowerCase().includes(searchQuery.toLowerCase()) ||
          entry.formattedTime.full.includes(searchQuery) ||
          entry.level.level.toLowerCase().includes(searchQuery.toLowerCase())
        if (!matchesSearch) return false
      }

      // Column-specific multi-select filters
      if (filterLevel.size > 0 && !filterLevel.has(entry.level.level)) {
        return false
      }
      if (filterSource.size > 0 && !filterSource.has(entry.source)) {
        return false
      }
      if (filterMessage.size > 0) {
        const matches = Array.from(filterMessage).some((filter) =>
          entry.message.toLowerCase().includes(filter.toLowerCase())
        )
        if (!matches) return false
      }

      return true
    })

    // Limit entries
    return {
      filtered: filtered.slice(0, maxEntries),
      uniqueLevels: Array.from(uniqueLevels).sort(),
      uniqueSources: Array.from(uniqueSources).sort(),
    }
  }, [events, searchQuery, filterLevel, filterSource, filterMessage, maxEntries])

  const { filtered: logEntries, uniqueLevels, uniqueSources } = logEntriesWithFilters

  // Track when new events arrive and mark them as new
  useEffect(() => {
    if (events.length > prevEventsLengthRef.current) {
      const newCount = events.length - prevEventsLengthRef.current
      // Mark the newest events as "new" for visual highlighting
      // Events are sorted newest first, so the first newCount events are the newest
      const newestEvents = events.slice(0, newCount)
      newestEvents.forEach((event) => {
        const eventId = `${event.timestamp}-${event.key}`
        newEventIdsRef.current.add(eventId)
        // Remove the "new" highlight after 3 seconds
        setTimeout(() => {
          newEventIdsRef.current.delete(eventId)
        }, 3000)
      })
      prevEventsLengthRef.current = events.length
    }
  }, [events.length, events])

  // Auto-scroll to top when new entries arrive (since newest are on top)
  useEffect(() => {
    if (autoScroll && scrollRef.current && events.length > prevEventsLengthRef.current) {
      // Use setTimeout to ensure DOM is updated after React render
      const timeoutId = setTimeout(() => {
        if (scrollRef.current) {
          // Scroll to top since newest logs are at the top
          scrollRef.current.scrollTop = 0
        }
      }, 50)
      return () => clearTimeout(timeoutId)
    }
  }, [events.length, autoScroll, logEntries.length])

  const handleScroll = () => {
    if (scrollRef.current) {
      const { scrollTop, scrollHeight, clientHeight } = scrollRef.current
      // If user scrolls up, disable auto-scroll
      if (scrollTop + clientHeight < scrollHeight - 100) {
        setAutoScroll(false)
      } else {
        setAutoScroll(true)
      }
    }
  }

  return (
    <Card>
      <CardHeader>
        <div className="flex items-center justify-between">
          <div>
            <CardTitle>Event Log</CardTitle>
            <CardDescription>
              Real-time ROS2 events and messages ({logEntries.length} entries)
            </CardDescription>
          </div>
          <div className="flex items-center gap-2">
            <div className="relative">
              <Search className="absolute left-2 top-1/2 transform -translate-y-1/2 h-4 w-4 text-muted-foreground" />
              <Input
                type="text"
                placeholder="Search logs..."
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                className="pl-8 pr-8 w-64"
              />
              {searchQuery && (
                <button
                  onClick={() => setSearchQuery('')}
                  className="absolute right-2 top-1/2 transform -translate-y-1/2"
                >
                  <X className="h-4 w-4 text-muted-foreground hover:text-foreground" />
                </button>
              )}
            </div>
          </div>
        </div>
      </CardHeader>
      <CardContent>
        <div className="border rounded-lg bg-muted/20 overflow-hidden">
          {/* Column Headers with Filters */}
          <div className="bg-muted/50 border-b sticky top-0 z-10">
            <div className="grid grid-cols-[120px_70px_200px_1fr] gap-3 px-4 py-2 font-semibold text-sm">
              <div className="text-muted-foreground">Timestamp</div>
              <div className="text-muted-foreground">Level</div>
              <div className="text-muted-foreground">Source</div>
              <div className="text-muted-foreground">Message</div>
            </div>
            <div className="grid grid-cols-[120px_70px_200px_1fr] gap-3 px-4 pb-2 relative">
              <div></div>
              <MultiSelect
                options={uniqueLevels}
                selected={filterLevel}
                onSelectionChange={setFilterLevel}
                placeholder="Filter level..."
                className="h-7 text-xs"
              />
              <MultiSelect
                options={uniqueSources}
                selected={filterSource}
                onSelectionChange={setFilterSource}
                placeholder="Filter source..."
                className="h-7 text-xs font-mono"
              />
              <Input
                type="text"
                placeholder="Filter message..."
                value={Array.from(filterMessage).join(', ')}
                onChange={(e) => {
                  const values = e.target.value.split(',').map((v) => v.trim()).filter(Boolean)
                  setFilterMessage(new Set(values))
                }}
                className="h-7 text-xs"
              />
            </div>
          </div>
          
          <div
            ref={scrollRef}
            onScroll={handleScroll}
            className="h-[600px] overflow-y-auto font-mono text-sm"
          >
            {logEntries.length === 0 ? (
              <div className="text-center text-muted-foreground py-8">
                {searchQuery ? 'No logs match your search' : 'No logs available'}
              </div>
            ) : (
              <div className="space-y-0.5">
                {logEntries.map((entry, idx) => {
                  return (
                    <div key={`${entry.timestamp}-${idx}`}>
                      <div className="grid grid-cols-[120px_70px_200px_1fr] gap-3 px-4 py-2 hover:bg-accent/50 transition-colors group border-l-2 border-transparent hover:border-primary/50 items-center">
                        <span className="text-muted-foreground shrink-0 font-mono text-sm">
                          {entry.formattedTime.time}
                        </span>
                        <span
                          className={`shrink-0 font-semibold text-sm ${entry.level.color}`}
                        >
                          {entry.level.level}
                        </span>
                        <span className="text-muted-foreground shrink-0 truncate font-mono text-sm">
                          {entry.source}
                        </span>
                        <span className="flex-1 truncate text-sm leading-normal whitespace-nowrap overflow-hidden text-ellipsis" title={entry.message}>
                          {entry.message}
                        </span>
                      </div>
                    </div>
                  )
                })}
              </div>
            )}
          </div>
        </div>
        <div className="mt-2 flex items-center justify-between text-xs text-muted-foreground">
          <div>
            Showing {logEntries.length} of {events.length} total events
            {(searchQuery || filterLevel.size > 0 || filterSource.size > 0 || filterMessage.size > 0) && (
              <span className="ml-2">
                (filtered
                {searchQuery && ` by search: "${searchQuery}"`}
                {filterLevel.size > 0 && ` level: ${filterLevel.size} selected`}
                {filterSource.size > 0 && ` source: ${filterSource.size} selected`}
                {filterMessage.size > 0 && ` message: ${filterMessage.size} selected`})
              </span>
            )}
          </div>
          <div className="flex items-center gap-2">
            <label className="flex items-center gap-1">
              <input
                type="checkbox"
                checked={autoScroll}
                onChange={(e) => setAutoScroll(e.target.checked)}
                className="rounded"
              />
              Auto-scroll
            </label>
          </div>
        </div>
      </CardContent>
    </Card>
  )
}

