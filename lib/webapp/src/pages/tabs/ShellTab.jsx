import { useState, useRef, useEffect, useCallback, forwardRef, useImperativeHandle } from 'react'
import { SquareTerminal, Network } from 'lucide-react'
import { wsUrl, API_HOST } from '../../api'

export const ShellTab = forwardRef(function ShellTab({ target: t, onStatusChange }, ref) {
  const termRef = useRef(null)
  const wsRef = useRef(null)
  const xtermRef = useRef(null)
  const [status, setStatus] = useState('idle')
  const prevStatus = useRef(status)
  if (prevStatus.current !== status) {
    prevStatus.current = status
    onStatusChange?.(status)
  }

  const initTerm = useCallback(async () => {
    if (xtermRef.current) return
    const { Terminal } = await import('@xterm/xterm')
    const { FitAddon } = await import('@xterm/addon-fit')
    await import('@xterm/xterm/css/xterm.css')
    const term = new Terminal({
      cursorBlink: true, fontSize: 13,
      fontFamily: 'JetBrains Mono, Fira Code, monospace',
      theme: {
        background: '#1a1028', foreground: '#e2ddf0', cursor: '#a78bfa',
        selectionBackground: '#463960',
        black: '#1a1028', red: '#dc2626', green: '#16a34a', yellow: '#eab308',
        blue: '#7c3aed', magenta: '#a78bfa', cyan: '#22d3ee', white: '#e2ddf0',
      },
    })
    const fitAddon = new FitAddon()
    term.loadAddon(fitAddon)
    if (!termRef.current) { term.dispose(); return }
    term.open(termRef.current)
    fitAddon.fit()
    xtermRef.current = term
    term.onData((data) => {
      if (wsRef.current?.readyState === WebSocket.OPEN) wsRef.current.send(data)
    })
    const sendSize = () => {
      fitAddon.fit()
      if (wsRef.current?.readyState === WebSocket.OPEN) {
        wsRef.current.send(JSON.stringify({ type: 'resize', cols: term.cols, rows: term.rows }))
      }
    }
    const ro = new ResizeObserver(sendSize)
    ro.observe(termRef.current)
    term._ro = ro
  }, [])

  const connect = useCallback(async () => {
    if (wsRef.current) return
    setStatus('connecting')
    if (xtermRef.current) xtermRef.current.clear()
    await initTerm()

    const ws = new WebSocket(wsUrl(`/shell?target=${encodeURIComponent(t.id)}`))
    ws.binaryType = 'arraybuffer'
    wsRef.current = ws
    ws.onmessage = (e) => {
      if (typeof e.data === 'string') {
        const msg = JSON.parse(e.data)
        if (msg.type === 'ready') {
          setStatus('connected')
          ws.send(JSON.stringify({ type: 'resize', cols: xtermRef.current.cols, rows: xtermRef.current.rows }))
          if (xtermRef.current) xtermRef.current.focus()
        } else if (msg.type === 'error') {
          const isBusy = (msg.msg || '').toLowerCase().includes('busy')
          setStatus(isBusy ? 'busy' : 'error')
          if (xtermRef.current) {
            if (isBusy) {
              xtermRef.current.writeln('\r\n\x1b[33mShell slot is in use (another session is active, possibly from the TUI).\x1b[0m')
              xtermRef.current.writeln('\x1b[33mClose the other shell first, then reconnect.\x1b[0m')
            } else {
              xtermRef.current.writeln(`\r\n\x1b[31m${msg.msg}\x1b[0m`)
            }
          }
        }
      } else if (xtermRef.current) {
        xtermRef.current.write(new Uint8Array(e.data))
      }
    }
    ws.onclose = () => {
      wsRef.current = null
      setStatus(prev => {
        if (prev === 'busy' || prev === 'error') return prev
        if (xtermRef.current) xtermRef.current.writeln('\r\n\x1b[33m[session ended]\x1b[0m')
        return 'disconnected'
      })
    }
    ws.onerror = () => ws.close()
  }, [t.id, initTerm])

  const disconnect = useCallback(() => {
    if (wsRef.current) { wsRef.current.close(); wsRef.current = null }
    setStatus('idle')
  }, [])

  useImperativeHandle(ref, () => ({ disconnect }), [disconnect])

  useEffect(() => () => {
    if (wsRef.current) wsRef.current.close()
    wsRef.current = null
    if (xtermRef.current) {
      if (xtermRef.current._ro) xtermRef.current._ro.disconnect()
      xtermRef.current.dispose()
      xtermRef.current = null
    }
  }, [])

  const active = status === 'connected' || status === 'connecting' || status === 'disconnected' || status === 'error' || status === 'busy'

  return (
    <div className="flex flex-col h-full">
      {active ? (
        <>
          <div className="flex items-center gap-2 px-4 py-2 border-b-2 border-border bg-bg-2">
            <SquareTerminal size={14} className="text-fg-3" />
            <span className="text-sm font-bold uppercase tracking-wide">root@target</span>
            <span className={`inline-block w-2 h-2 rounded-full ${
              status === 'connected' ? 'bg-green-500' :
              status === 'connecting' ? 'bg-yellow-500 animate-pulse' :
              status === 'busy' ? 'bg-amber' :
              'bg-neutral-500'
            }`} />
            <span className={`text-xs ${status === 'busy' ? 'text-amber font-bold' : 'text-fg-3'}`}>
              {status === 'connected' ? 'live' : status === 'connecting' ? 'opening...' : status === 'busy' ? 'slot in use' : 'closed'}
            </span>
            <div className="ml-auto flex items-center gap-2">
              {(status === 'disconnected' || status === 'error' || status === 'busy') && (
                <button className="nb-btn-main px-3 py-1 text-xs" onClick={connect} disabled={!t.online}>
                  Reconnect
                </button>
              )}
              {(status === 'connected' || status === 'connecting') && (
                <button className="nb-btn px-3 py-1 text-xs" onClick={disconnect}>
                  Disconnect
                </button>
              )}
            </div>
          </div>
          <div ref={termRef} className="flex-1 bg-[#1a1028]" style={{ padding: '8px' }} />
        </>
      ) : (
        <div className="flex-1 flex items-center justify-center h-full">
          <div className="px-8 py-10 max-w-md w-full">
            <div className="text-center mb-8">
              <div className="text-lg font-bold">Interactive Shell</div>
              <div className="text-xs text-fg-3 mt-1">Persistent guest PTY over the encrypted control channel</div>
            </div>

            <div className="border-2 border-border rounded-[var(--radius)] bg-bg-2 shadow-[4px_4px_0px_0px_var(--color-border)] p-4 mb-8">
              <div className="flex items-stretch gap-0">
                <div className="flex-1 min-w-0 border-2 border-border rounded-[var(--radius)] bg-[#1a1028] p-3">
                  <div className="flex items-center gap-1.5 mb-2">
                    <SquareTerminal size={12} className="text-[#a78bfa] shrink-0" />
                    <span className="text-[10px] font-bold uppercase tracking-wider text-[#a78bfa]">Target</span>
                  </div>
                  <div className="text-[11px] font-mono text-white/90 truncate">{t.hostname || t.id}</div>
                  {t.peer && <div className="text-[10px] font-mono text-white/50 mt-0.5 truncate">{t.peer}</div>}
                  {t.kernel && <div className="text-[10px] font-mono text-white/40 mt-0.5 truncate">{t.kernel}</div>}
                </div>

                <div className="flex flex-col items-center justify-center w-14 shrink-0">
                  <svg width="56" height="24" viewBox="0 0 56 24" className="text-main">
                    <line x1="0" y1="12" x2="46" y2="12" stroke="currentColor" strokeWidth="2" strokeDasharray="4 3" />
                    <polygon points="46,7 56,12 46,17" fill="currentColor" />
                  </svg>
                  <span className="text-[8px] font-bold uppercase tracking-widest text-fg-3 mt-0.5">PTY</span>
                </div>

                <div className="flex-1 min-w-0 border-2 border-border rounded-[var(--radius)] bg-bg p-3">
                  <div className="flex items-center gap-1.5 mb-2">
                    <Network size={12} className="text-main shrink-0" />
                    <span className="text-[10px] font-bold uppercase tracking-wider text-main">Operator</span>
                  </div>
                  <div className="text-[11px] font-mono text-fg truncate">{API_HOST}</div>
                  <div className="text-[10px] font-mono text-fg-3 mt-0.5">ws:/shell</div>
                </div>
              </div>
            </div>

            <div className="text-center">
              <button
                className="nb-btn-main px-6 py-2.5 text-sm font-bold inline-flex items-center gap-2"
                onClick={connect}
                disabled={!t.online}
              >
                <SquareTerminal size={16} /> Connect
              </button>
              {!t.online && (
                <div className="text-sm text-red mt-4 font-bold">Target offline</div>
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  )
})
