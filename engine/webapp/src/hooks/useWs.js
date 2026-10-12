import { useState, useEffect, useRef, useCallback } from 'react'

export function useWs() {
  const [targets, setTargets] = useState([])
  const [events, setEvents] = useState([])
  const [log, setLog] = useState([])
  const [connected, setConnected] = useState(false)
  const wsRef = useRef(null)
  const retriesRef = useRef(0)
  const pendingRef = useRef({})
  const disposedRef = useRef(false)

  const connect = useCallback(() => {
    const host = location.hostname || 'localhost'
    const port = location.port === '5173' ? 8080 : (location.port || '8080')
    try {
      const ws = new WebSocket(`ws://${host}:${port}/ws`)
      ws.onopen = () => {
        wsRef.current = ws
        setConnected(true)
        retriesRef.current = 0
      }
      ws.onmessage = (e) => {
        const msg = JSON.parse(e.data)
        if (msg.type === 'state') {
          setTargets(msg.targets || [])
          setEvents(msg.events || [])
        } else if (msg.type === 'result') {
          setLog(prev => [{
            time: new Date().toLocaleTimeString(),
            cmd: msg.command, ok: msg.ok, reply: msg.reply, target: msg.target,
          }, ...prev])
          if (msg.ok) refresh()
          const key = `${msg.target}:${msg.command}`
          if (pendingRef.current[key]) {
            pendingRef.current[key].forEach(r => r(msg))
            delete pendingRef.current[key]
          }
        } else if (msg.type === 'event') {
          setEvents(prev => [...prev, msg.event])
        }
      }
      ws.onclose = () => {
        wsRef.current = null
        setConnected(false)
        for (const key of Object.keys(pendingRef.current)) {
          pendingRef.current[key].forEach(r => r({ ok: false, reply: 'disconnected' }))
        }
        pendingRef.current = {}
        if (!disposedRef.current) setTimeout(connect, 3000)
      }
      ws.onerror = () => ws.close()
    } catch {
      setTimeout(connect, 3000)
    }
  }, [])

  const refresh = useCallback(() => {
    if (wsRef.current?.readyState === 1)
      wsRef.current.send(JSON.stringify({ type: 'refresh' }))
  }, [])

  const sendCmd = useCallback((command, targetId) => {
    if (!targetId || !command) return
    setLog(prev => [{
      time: new Date().toLocaleTimeString(),
      cmd: command, ok: null, reply: '(pending)', target: targetId,
    }, ...prev])
    if (wsRef.current?.readyState === 1) {
      wsRef.current.send(JSON.stringify({ type: 'command', target: targetId, command }))
    }
  }, [])

  const query = useCallback((command, targetId) => {
    if (!targetId || !command) return Promise.resolve({ ok: false, reply: 'missing args' })
    if (wsRef.current?.readyState === 1) {
      return new Promise((resolve) => {
        const key = `${targetId}:${command}`
        if (!pendingRef.current[key]) pendingRef.current[key] = []
        pendingRef.current[key].push(resolve)
        wsRef.current.send(JSON.stringify({ type: 'command', target: targetId, command }))
      })
    }
    return Promise.resolve({ ok: false, reply: 'not connected' })
  }, [])

  useEffect(() => {
    disposedRef.current = false
    connect()
    return () => {
      disposedRef.current = true
      wsRef.current?.close()
    }
  }, [connect])

  return { targets, events, log, connected, sendCmd, query, refresh }
}
