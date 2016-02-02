require 'pxp-agent/test_helper.rb'
require 'json'

ENVIRONMENT_NAME = 'sleep'
MODULE_NAME = 'sleep'
SECONDS_TO_SLEEP = 500 # The test will use SIGALARM to end this as soon as required
STATUS_QUERY_MAX_RETRIES = 60
STATUS_QUERY_INTERVAL_SECONDS = 5

test_name 'C94705 - Run Puppet (non-blocking request) and restart pxp-agent service during run' do
  step 'On master, create a new environment with a module that will take some time' do
    environmentpath = master.puppet['environmentpath']
    site_manifest = "#{environmentpath}/#{ENVIRONMENT_NAME}/manifests/site.pp"
    on(master, "cp -r #{environmentpath}/production #{environmentpath}/#{ENVIRONMENT_NAME}")
    create_remote_file(master, site_manifest, <<-SITEPP)
node default {
  include #{MODULE_NAME}
}
SITEPP
    on(master, "chmod 644 #{site_manifest}")
    on(master, "mkdir -p #{environmentpath}/#{ENVIRONMENT_NAME}/modules/#{MODULE_NAME}/manifests")
    module_manifest = "#{environmentpath}/#{ENVIRONMENT_NAME}/modules/#{MODULE_NAME}/manifests/init.pp"
    create_remote_file(master, module_manifest, <<-MODULEPP)
class #{MODULE_NAME} {
  exec { 'sleep':
   command => '/bin/sleep #{SECONDS_TO_SLEEP} || true', # or true to ignore the exit code of sleep
  }
}
MODULEPP
    on(master, "chmod 644 #{module_manifest}")
  end

  agents.each_with_index do |agent, i|
    agent_identity = "pcp://client0#{i+1}.example.com/agent"
    transaction_id = nil
    step "Make a non-blocking puppet run request on #{agent}" do
      provisional_responses = rpc_non_blocking_request(master, [agent_identity],
                                      'pxp-module-puppet', 'run',
                                      {:env => [], :flags => ['--server', "'#{master}'",
                                                              '--onetime',
                                                              '--no-daemonize',
                                                              "--environment", "'#{ENVIRONMENT_NAME}'"]})
      transaction_id = provisional_responses[agent_identity][:data]["transaction_id"]
    end

    step 'Wait a couple of seconds to ensure that Puppet has time to execute manifest' do
      sleep 2
    end

    step "Restart pxp-agent service on #{agent}" do
      on agent, puppet('resource service pxp-agent ensure=stopped')
      on agent, puppet('resource service pxp-agent ensure=running')
    end

    step 'Signal sleep to end so Puppet run will complete' do
      on(agent, "ps -ef | grep '/bin/sleep' | grep -v 'grep' | grep -v 'true' | sed 's/^[^0-9]*//g' | cut -d\\  -f1") do |output|
        pid = output.stdout.chomp
        if (!pid || pid == '')
          fail('Did not find a pid for the sleep process holding up Puppet - cannot test PXP response if Puppet wasn\'t sleeping')
        end
        on(agent, "kill SIGALARM #{pid}")
      end
    end

    step 'Check response of puppet run' do
      # TODO - following PCP-208 we can and should query for the action status
      # Currently we get the stdout and stderr but action status remains 'unknown'
      puppet_run_result = nil
      query_attempts = 0
      until (query_attempts == STATUS_QUERY_MAX_RETRIES || puppet_run_result) do
        query_responses = rpc_blocking_request(master, [agent_identity],
                                        'status', 'query', {:transaction_id => transaction_id})
        action_result = query_responses[agent_identity][:data]["results"]
        if (action_result.has_key?('stdout'))
          puppet_run_result = JSON.parse(action_result['stdout'])['status']
        end
        query_attempts += 1
        if (!puppet_run_result)
          sleep STATUS_QUERY_INTERVAL_SECONDS
        end
      end
      if (!puppet_run_result)
        fail("Run puppet non-blocking transaction did not contain stdout of puppet run after #{query_attempts} attempts " \
             "and #{query_attempts * STATUS_QUERY_INTERVAL_SECONDS} seconds")
      else
        assert_equal(puppet_run_result, "changed", "Puppet run did not have expected result of 'changed'")
      end
    end
  end
end
