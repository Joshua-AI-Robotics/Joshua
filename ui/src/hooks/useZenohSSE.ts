import { useEffect, useState, useRef } from 'react'

export interface ZenohEvent {
  timestamp: number
  key: string
  value: any
}

export interface UseZenohSSEOptions {
  enabled?: boolean
  bridgeUrl?: string
  path?: string
  useProxy?: boolean  // Use /api/zenoh proxy instead of direct connection
  pollInterval?: number  // Polling interval in milliseconds (default: 2000ms)
}

/**
 * Hook to subscribe to Zenoh REST API via polling
 * 
 * @param path - Zenoh key path (e.g., '@/local/ros2/node/**' or '@/local/ros2/route/**')
 * @param options - Configuration options
 * @returns Array of events received from polling the REST API
 * 
 * @example
 * const nodes = useZenohSSE('@/local/ros2/node/**')
 * const routes = useZenohSSE('@/local/ros2/route/**', { pollInterval: 1000 })
 */
export function useZenohSSE(
  path: string,
  options: UseZenohSSEOptions = {}
): { events: ZenohEvent[]; error: Error | null; connected: boolean } {
  const {
    enabled = true,
    bridgeUrl = 'http://localhost:8000',
    useProxy = true,  // Default to using proxy to avoid CORS
    pollInterval = 2000,  // Poll every 2 seconds by default
  } = options

  const [events, setEvents] = useState<ZenohEvent[]>([])
  const [error, setError] = useState<Error | null>(null)
  const [connected, setConnected] = useState(false)
  const pollIntervalRef = useRef<number | null>(null)
  const lastDataRef = useRef<string>('') // Track last data to detect changes

  useEffect(() => {
    if (!enabled) {
      return
    }

    // Clean up previous polling
    if (pollIntervalRef.current) {
      clearInterval(pollIntervalRef.current)
      pollIntervalRef.current = null
    }

    setError(null)
    setConnected(false)
    setEvents([]) // Clear previous events when starting new connection
    lastDataRef.current = ''

    // Construct the base URL
    // Use proxy if enabled (avoids CORS), otherwise direct connection
    const baseUrl = useProxy ? '/api/zenoh' : bridgeUrl
    // Remove leading slash from path if present to avoid double slashes
    const cleanPath = path.startsWith('/') ? path.slice(1) : path
    const restUrl = `${baseUrl}/${cleanPath}`

    // Function to fetch and update data
    const fetchData = async () => {
      try {
        const response = await fetch(restUrl)
        if (!response.ok) {
          throw new Error(`HTTP error! status: ${response.status}`)
        }
        const data = await response.json()
        
        // Convert data to events
        const newEvents: ZenohEvent[] = Array.isArray(data)
          ? data.map((item: any) => ({
              timestamp: Date.now(),
              key: item.key || 'unknown',
              value: item.value || item,
            }))
          : data
            ? [
                {
                  timestamp: Date.now(),
                  key: data.key || path,
                  value: data.value || data,
                },
              ]
            : []
        
        // Check if data has changed by comparing JSON string
        const currentDataStr = JSON.stringify(data)
        if (currentDataStr !== lastDataRef.current) {
          // Data has changed - update events
          setEvents((prev) => {
            // For logs (path includes '**'), append all new events
            // For nodes/routes, replace with latest data
            if (path.includes('**') && path.includes('ros2')) {
              // Logs: only add events that are truly new (by key + timestamp)
              const existingEventIds = new Set(
                prev.map((e) => `${e.timestamp}-${e.key}`)
              )
              const trulyNewEvents = newEvents.filter(
                (e) => !existingEventIds.has(`${e.timestamp}-${e.key}`)
              )
              
              if (trulyNewEvents.length > 0) {
                // Prepend new events (newest first)
                return [...trulyNewEvents, ...prev]
              }
              return prev
            } else {
              // Nodes/Routes: replace with latest data, but only add truly new items
              const existingKeys = new Set(prev.map((e) => e.key))
              const newEventsToAdd = newEvents.filter((e) => !existingKeys.has(e.key))
              const updatedEvents = prev.map((prevEvent) => {
                const updated = newEvents.find((e) => e.key === prevEvent.key)
                return updated || prevEvent
              })

              return [...updatedEvents, ...newEventsToAdd]
            }
          })
          lastDataRef.current = currentDataStr
        }
        
        setConnected(true)
        setError(null)
      } catch (err) {
        console.error('Polling error:', err)
        setError(err instanceof Error ? err : new Error('Polling failed'))
        setConnected(false)
      }
    }
    
    // Initial fetch
    fetchData()
    
    // Set up polling interval
    pollIntervalRef.current = window.setInterval(fetchData, pollInterval)

    // Cleanup on unmount
    return () => {
      if (pollIntervalRef.current) {
        clearInterval(pollIntervalRef.current)
        pollIntervalRef.current = null
      }
      setConnected(false)
    }
  }, [path, bridgeUrl, enabled, useProxy, pollInterval])

  return { events, error, connected }
}

