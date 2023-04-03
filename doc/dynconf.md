
# Support for dynamic configuration changes

The idea is that when the config file is overwritten, we read it again, and apply the changes compared to the old config. We need to be able to

* Detect if something has changed
* Add new stuff that wasn't in the old config
* Remove stuff that was in the old config but not in the new one
* Do not disturb the stuff that is the same in both configs

## Interaction with other configuration

If we support configuration via NetConf or OpenFlow, how do those changes interact with DynConf?

