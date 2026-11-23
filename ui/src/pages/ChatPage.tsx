import { FormEvent, useEffect, useRef, useState } from 'react'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'

interface Turn {
  sequence: number
  text: string
  reply?: string
}

async function post(endpoint: string, signal: AbortSignal, session?: string, payload?: object) {
  const response = await fetch('/api/chat/' + endpoint, {
    method: 'POST',
    signal,
    headers: {
      'Content-Type': 'application/json',
      ...(session ? { Authorization: 'Bearer ' + session } : {}),
    },
    body: JSON.stringify(payload ?? {}),
  })
  if (!response.ok) {
    const message = response.headers.get('content-type')?.startsWith('text/plain')
      ? await response.text()
      : 'Chat is unavailable. Check the chat service and try again.'
    throw new Error(message)
  }
  return response.json()
}

export default function ChatPage() {
  const [turns, setTurns] = useState<Turn[]>([])
  const [draft, setDraft] = useState('')
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)
  const transcript = useRef<HTMLDivElement>(null)
  const session = useRef('')
  const active = useRef<AbortController | null>(null)
  const last = turns[turns.length - 1]
  const pending = last && last.reply === undefined

  useEffect(() => () => { active.current?.abort() }, [])
  useEffect(() => {
    const log = transcript.current
    if (log) log.scrollTop = log.scrollHeight
  }, [turns])

  function reset() {
    active.current?.abort()
    active.current = null
    session.current = ''
    setTurns([])
    setDraft('')
    setError('')
    setBusy(false)
  }

  async function send(event: FormEvent) {
    event.preventDefault()
    if (active.current || (!pending && !draft.trim())) return
    const turn = pending ? last : { sequence: (last?.sequence ?? 0) + 1, text: draft.trim() }
    const controller = new AbortController()
    active.current = controller
    setBusy(true)
    setError('')
    if (!pending) {
      setTurns(current => [...current.slice(-49), turn])
      setDraft('')
    }
    try {
      if (!session.current) {
        const data = await post('session', controller.signal)
        if (controller.signal.aborted) return
        session.current = data.session
      }
      const result = await post('turn', controller.signal, session.current, turn)
      if (controller.signal.aborted) return
      if (result.sequence !== turn.sequence || typeof result.reply !== 'string') {
        throw new Error('Chat returned an invalid reply.')
      }
      setTurns(current => [...current.slice(0, -1), { ...turn, reply: result.reply }])
    } catch (reason) {
      if (!controller.signal.aborted) {
        setError(reason instanceof Error ? reason.message : 'Unable to reach chat.')
      }
    } finally {
      if (active.current === controller) {
        active.current = null
        setBusy(false)
      }
    }
  }

  return (
    <section className="max-w-3xl space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-3xl font-bold tracking-tight">Chat</h2>
        {turns.length > 0 && <Button variant="outline" onClick={reset}>New chat</Button>}
      </div>
      <div ref={transcript} role="log" aria-label="Conversation" aria-live="polite"
        className="max-h-[55vh] space-y-4 overflow-y-auto">
        {turns.map(turn => (
          <div key={turn.sequence} className="space-y-2">
            <p className="ml-auto w-fit max-w-full whitespace-pre-wrap break-words rounded-lg bg-muted px-4 py-2">
              <span className="sr-only">You: </span>{turn.text}
            </p>
            {turn.reply && <p className="whitespace-pre-wrap break-words">
              <span className="font-medium">Joshua</span><br />{turn.reply}
            </p>}
          </div>
        ))}
      </div>
      <form onSubmit={send} className="space-y-2">
        <label htmlFor="chat-message" className="text-sm font-medium">Message Joshua</label>
        <div className="flex gap-2">
          <Input id="chat-message" value={draft} maxLength={2000}
            placeholder="Ask a question or check the ROS nodes"
            disabled={busy || pending} onChange={event => setDraft(event.target.value)} />
          <Button type="submit" disabled={busy || (!pending && !draft.trim())}>
            {pending && !busy ? 'Retry' : 'Send'}
          </Button>
        </div>
        {busy && <p role="status" className="text-sm text-muted-foreground">Waiting for Joshua…</p>}
        {error && <p role="alert" className="text-sm text-destructive">{error}</p>}
      </form>
    </section>
  )
}
