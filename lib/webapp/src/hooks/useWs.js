import { useState, useEffect, useRef, useCallback } from 'react'
import { wsUrl } from '../api'

export function useWs() {
  const [targets, setTargets] = useState([])
  const [events, setEvents] = useState([])
  const [commands, setCommands] = useState([])
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

  const refreshRef = useRef(null)

  const handlers = useRef({
    state(msg) {
      setTargets(prev => {
        const incoming = msg.targets || []
        if (!prev.length) return incoming
        return incoming.map(t => {
          const old = prev.find(p => p.id === t.id)
          return old?.hidden ? { ...t, hidden: old.hidden } : t
        })
      })
      setEvents(msg.events || [])
      if (msg.commands) setCommands(msg.commands)
    },

    target_update(msg) {
      setTargets(prev => prev.map(t => t.id === msg.target.id ? msg.target : t))
    },

    event(msg) {
      setEvents(prev => [...prev, msg.event])
    },

    result(msg) {
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
      if (msg.ok) refreshRef.current?.()
      const key = `${msg.target}:${msg.command}`
      if (pendingRef.current[key]) {
        pendingRef.current[key].forEach(r => r(msg))
        delete pendingRef.current[key]
      }
    },

    exec_result(msg) {
      const id = msg._execId
      if (id != null && execPendingRef.current[id]) {
        execPendingRef.current[id](msg)
        delete execPendingRef.current[id]
      }
    },

    download_progress(msg) {
      const id = msg._dlId
      if (id != null && dlProgressRef.current[id]) {
        dlProgressRef.current[id](msg.received, msg.total)
      }
    },

    download_result(msg) {
      const id = msg._dlId
      if (id != null) {
        delete dlProgressRef.current[id]
        if (dlPendingRef.current[id]) {
          dlPendingRef.current[id](msg)
          delete dlPendingRef.current[id]
        }
      }
    },
  })

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
        const handler = handlers.current[msg.type]
        if (handler) handler(msg)
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

  const send = useCallback((payload) => {
    if (wsRef.current?.readyState === 1) {
      wsRef.current.send(JSON.stringify(payload))
      return true
    }
    return false
  }, [])

  const refresh = useCallback(() => send({ type: 'refresh' }), [send])
  refreshRef.current = refresh

  const watchTarget = useCallback((targetId) => {
    send({ type: 'watch', target: targetId || null })
  }, [send])

  const renameTarget = useCallback((targetId, label) => {
    send({ type: 'rename', target: targetId, label })
  }, [send])

  const sendCmd = useCallback((command, targetId) => {
    if (!targetId || !command) return
    setLog(prev => [{
      time: new Date().toLocaleTimeString(),
      cmd: command, ok: null, reply: '(pending)', target: targetId,
    }, ...prev])
    send({ type: 'command', target: targetId, command })
  }, [send])

  const query = useCallback((command, targetId) => {
    if (!targetId || !command) return Promise.resolve({ ok: false, reply: 'missing args' })
    if (send({ type: 'command', target: targetId, command })) {
      return new Promise((resolve) => {
        const key = `${targetId}:${command}`
        if (!pendingRef.current[key]) pendingRef.current[key] = []
        pendingRef.current[key].push(resolve)
      })
    }
    return Promise.resolve({ ok: false, reply: 'not connected' })
  }, [send])

  const execCmd = useCallback((shellCommand, targetId) => {
    if (!targetId || !shellCommand) return Promise.resolve({ ok: false, output: 'missing args' })
    const id = ++execIdRef.current
    if (send({ type: 'exec', target: targetId, command: shellCommand, _execId: id })) {
      return new Promise((resolve) => { execPendingRef.current[id] = resolve })
    }
    return Promise.resolve({ ok: false, output: 'not connected' })
  }, [send])

  const downloadFile = useCallback((path, targetId, onProgress) => {
    if (!targetId || !path) return Promise.resolve({ ok: false, error: 'missing args' })
    const id = ++dlIdRef.current
    if (send({ type: 'download', target: targetId, path, _dlId: id })) {
      return new Promise((resolve) => {
        dlPendingRef.current[id] = resolve
        if (onProgress) dlProgressRef.current[id] = onProgress
      })
    }
    return Promise.resolve({ ok: false, error: 'not connected' })
  }, [send])

  useEffect(() => {
    disposedRef.current = false
    connect()
    return () => {
      disposedRef.current = true
      wsRef.current?.close()
    }
  }, [connect])

  useEffect(() => {
    const id = setInterval(() => send({ type: 'heartbeat' }), 30000)
    return () => clearInterval(id)
  }, [send])

  return { targets, events, commands, log, connected, sendCmd, query, execCmd, downloadFile, refresh, renameTarget, watchTarget }
}
