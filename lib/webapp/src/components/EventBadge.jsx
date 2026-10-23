const styles = {
  'agent.connected':    'bg-green/15 text-green border-green/30',
  'agent.disconnected': 'bg-red/15 text-red border-red/30',
  'command':            'bg-blue/15 text-blue border-blue/30',
  'deploy':             'bg-main/15 text-main border-main/30',
  'pipeline':           'bg-main/15 text-main border-main/30',
}

const labels = {
  'agent.connected':    'CONNECTED',
  'agent.disconnected': 'DISCONNECTED',
  'command':            'COMMAND',
  'deploy':             'DEPLOY',
  'pipeline':           'PIPELINE',
}

export function EventBadge({ type }) {
  const style = styles[type] || 'bg-bg-2 text-fg-3 border-border'
  const label = labels[type] || type?.toUpperCase() || '?'
  return <span className={`text-[9px] font-bold tracking-wider px-2 py-0.5 rounded border-2 ${style} shrink-0`}>{label}</span>
}
