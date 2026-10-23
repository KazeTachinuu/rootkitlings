import { StatusPill } from '../../components/Badge'
import { EventBadge } from '../../components/EventBadge'
import { Th } from '../../components/Th'

export function LogTab({ entries }) {
  if (!entries.length) return <div className="p-8 text-center text-fg-3">No commands sent yet</div>
  return (
    <div className="overflow-auto">
      <table className="w-full text-sm">
        <thead>
          <tr className="border-b-2 border-border bg-bg-2 sticky top-0">
            <Th>Time</Th><Th>Command</Th><Th>Status</Th><Th>Response</Th>
          </tr>
        </thead>
        <tbody>
          {entries.map((l, i) => (
            <tr key={i} className="border-b border-border">
              <td className="px-4 py-2 font-mono text-fg-3">{l.time}</td>
              <td className="px-4 py-2 font-mono font-bold">{l.cmd}</td>
              <td className="px-4 py-2"><StatusPill ok={l.ok} /></td>
              <td className="px-4 py-2 font-mono text-fg-3 truncate max-w-xs">{l.reply}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

function formatTime(ts) {
  if (!ts) return ''
  const d = new Date(ts.includes('T') ? ts : ts.replace(' ', 'T'))
  if (isNaN(d)) return ts
  const pad = n => String(n).padStart(2, '0')
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}

export function EventsTab({ events }) {
  if (!events.length) return <div className="p-8 text-center text-fg-3">No events yet</div>
  return (
    <div className="overflow-auto">
      {[...events].reverse().map((e, i) => (
        <div key={i} className="flex items-start gap-3 px-4 py-2.5 border-b border-border hover:bg-bg-2">
          <span className="font-mono text-[10px] text-fg-3 shrink-0 pt-0.5">{formatTime(e.time)}</span>
          <EventBadge type={e.type} />
          <span className="font-mono text-xs text-fg-2 min-w-0 break-all">{e.msg}</span>
        </div>
      ))}
    </div>
  )
}
