import { useState, useEffect, useRef, useCallback } from 'react'
import { wsUrl } from '../api'

export function useWs() {
  const [targets, setTargets] = useState([])
  const [events, setEvents] = useState([])
  const [log, setLog] = useState([])
  const [connected, setConnected] = useState(false)
  const wsRef = useRef(null)
  const retriesRef = useRef(0)
  const pendingRef = useRef({})
  const execPendingRef = useRef({})
  const execIdRef = useRef(0)
  const dlPendingRef = useRef({})
  const dlProgressRef = useRef({})
  const dlIdRef = useRef(0)
  const disposedRef = useRef(false)

  const connect = useCallback(() => {
    try {
      const ws = new WebSocket(wsUrl('/ws'))
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
        } else if (msg.type === 'target_update') {
          setTargets(prev => prev.map(t => t.id === msg.target.id ? msg.target : t))
        } else if (msg.type === 'result') {
          setLog(prev => {
            const idx = prev.findIndex(l => l.target === msg.target && l.cmd === msg.command && l.ok === null)
            if (idx >= 0) {
              const updated = [...prev]
              updated[idx] = { ...updated[idx], ok: msg.ok, reply: msg.reply }
              return updated
            }
            return [{ time: new Date().toLocaleTimeString(),
              cmd: msg.command, ok: msg.ok, reply: msg.reply, target: msg.target }, ...prev]
          })
          if (msg.ok) refresh()
          const key = `${msg.target}:${msg.command}`
          if (pendingRef.current[key]) {
            pendingRef.current[key].forEach(r => r(msg))
            delete pendingRef.current[key]
          }
        } else if (msg.type === 'exec_result') {
          const id = msg._execId
          if (id != null && execPendingRef.current[id]) {
            execPendingRef.current[id](msg)
            delete execPendingRef.current[id]
          }
        } else if (msg.type === 'download_progress') {
          const id = msg._dlId
          if (id != null && dlProgressRef.current[id]) {
            dlProgressRef.current[id](msg.received, msg.total)
          }
        } else if (msg.type === 'download_result') {
          const id = msg._dlId
          if (id != null) {
            delete dlProgressRef.current[id]
            if (dlPendingRef.current[id]) {
              dlPendingRef.current[id](msg)
              delete dlPendingRef.current[id]
            }
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
        for (const id of Object.keys(execPendingRef.current)) {
          execPendingRef.current[id]({ ok: false, output: 'disconnected' })
        }
        execPendingRef.current = {}
        for (const id of Object.keys(dlPendingRef.current)) {
          dlPendingRef.current[id]({ ok: false, error: 'disconnected' })
        }
        dlPendingRef.current = {}
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

  const watchTarget = useCallback((targetId) => {
    if (wsRef.current?.readyState === 1)
      wsRef.current.send(JSON.stringify({ type: 'watch', target: targetId || null }))
  }, [])

  const renameTarget = useCallback((targetId, label) => {
    if (wsRef.current?.readyState === 1)
      wsRef.current.send(JSON.stringify({ type: 'rename', target: targetId, label }))
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

  const execCmd = useCallback((shellCommand, targetId) => {
    if (!targetId || !shellCommand) return Promise.resolve({ ok: false, output: 'missing args' })
    if (wsRef.current?.readyState === 1) {
      const id = ++execIdRef.current
      return new Promise((resolve) => {
        execPendingRef.current[id] = resolve
        wsRef.current.send(JSON.stringify({
          type: 'exec', target: targetId, command: shellCommand, _execId: id,
        }))
      })
    }
    return Promise.resolve({ ok: false, output: 'not connected' })
  }, [])

  const downloadFile = useCallback((path, targetId, onProgress) => {
    if (!targetId || !path) return Promise.resolve({ ok: false, error: 'missing args' })
    if (wsRef.current?.readyState === 1) {
      const id = ++dlIdRef.current
      return new Promise((resolve) => {
        dlPendingRef.current[id] = resolve
        if (onProgress) dlProgressRef.current[id] = onProgress
        wsRef.current.send(JSON.stringify({
          type: 'download', target: targetId, path, _dlId: id,
        }))
      })
    }
    return Promise.resolve({ ok: false, error: 'not connected' })
  }, [])

  useEffect(() => {
    disposedRef.current = false
    connect()
    return () => {
      disposedRef.current = true
      wsRef.current?.close()
    }
  }, [connect])

  useEffect(() => {
    const id = setInterval(() => {
      if (wsRef.current?.readyState === 1)
        wsRef.current.send(JSON.stringify({ type: 'refresh' }))
    }, 5000)
    return () => clearInterval(id)
  }, [])

  return { targets, events, log, connected, sendCmd, query, execCmd, downloadFile, refresh, renameTarget, watchTarget }
}
