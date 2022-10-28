getpwuid_r
===

For diagnostic purposes, this is a small wrapper over getpwuid_r that avoids
heap allocations.  Pass it a combination of UIDs and usernames, it will loop
over them infinitely and get the opposite (UID->user, user->UID).
