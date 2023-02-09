# Action pipeline

The `pipeline.c` contains such a pipeline concept, where the iterator is the packet itself.

```
        ┌──────┐
        │packet│
        ├──────┤
        │ iter ├─┐
        └──────┘ │
                 │
                 ▼
    ─────────────┬───────────────────┬─────────────────────────────┐
Pipeline 1       │                   │                             │
                ┌▼─────────┐    ┌────▼─────┐    ┌──────────┐    ┌──▼───────┐
                │          │    │          │    │          │    │          │
                │ Action A │    │ Action B │    │ Action C │    │ Action D │
                │          │    │          │    │          │    │          │
                └─▲────────┘    └─▲────────┘    └─▲────────┘    └──────────┘
Pipeline 2        │               │               │
    ──────────────┴───────────────┴───────────────┘
                                                  ▲
                                        ┌──────┐  │
                                        │packet│  │
                                        ├──────┤  │
                                        │ iter ├──┘
                                        └──────┘
```

Consider the figure above. We have actions, and pipelines which technically list of pointers to actions.
The pipeline gets the packet and carry through that list of actions one by one.
Its important that some actions can drop the packets, some of them repliceate it to multiple pipelines (demux or mux).
Also some actions might have their own business logic with detours of the regular pipeline.
For example Delay action or POF action have its own worker thread with a queue/queues which temporarly carry the packet.

## Pipeline detour

When actions have their own queues and private processing pipelines, its important to preserve the packet place in the original pipeline.

```
       ────────┬──────────────┬─────────────────────────────────┐
               │              │                                 │
           ┌───▼─────┐     ┌──▼─────────┐                 ┌─────▼───┐
Receive    │ Action  │     │   Action   │                 │ Action  │
context    │    A    │     │ POF/Delay  │                 │    C    │
           └─────────┘     └──┬─────────┘                 └─────▲───┘
                              │                                 │
  ────────────────────────────┼─────────────────────────────────┼───────────
                           ***│*********************************│****
     Action worker         * ┌▼─────┐  ┌──────┐  ┌──────┐  ┌────┴─┐ *
        context            * │packet│  │packet│  │packet│  │packet│ *
                           * ├──────┤  ├──────┤  ├──────┤  ├──────┤ *
                           * │ iter │  │ iter │  │ iter │  │ iter │ *
                           * └──────┘  └──────┘  └──────┘  └──────┘ *
                           ******************************************
                                         POF/Delay queue
                                      (pipeline iteratr intact)
```

The figure above show that the action can steal the packet temporarly from the pipeline.
But the iterator of the packet not modified, therefore at any place of the processing it can contionue the original pipeline.
Benefits of this solution:
1. No overview of the full pipeline required for building it or executing it
2. The lifetime of the packet defined: if the iterator at the last action, the packet automatically deleted.

The main problem of the solution:
1. The iterator to the next pipeline element explicitly set by the actions.
This is important, however can be done with a convinient helper function.
That helper can also free the resources after the packet reach the end of the pipeline.
