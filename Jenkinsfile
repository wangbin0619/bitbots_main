@Library('bitbots_jenkins_library')_

pipeline {
    agent any

	triggers {
		cron 'H 6 * * * '
	}

    stages {
        stage('Build docker container') {
            when { branch 'master' }
            steps {
                sh 'docker build -t registry.bit-bots.de:5000/bitbots_builder --no-cache docker_builder'
                sh 'docker push registry.bit-bots.de:5000/bitbots_builder'
                sh 'docker image prune -f'
                sh 'docker container prune -f'
            }
        }

        stage('Document') {
            agent { docker image: 'bitbots_builder', registryUrl: 'http://registry.bit-bots.de:5000', alwaysPull: true }
            steps {
                linkCatkinWorkspace()
                catkinBuild("Documentation")

                stash includes: 'bitbots_docs/docs/_out/**', name: 'docs_output'
                publishHTML([allowMissing: false, alwaysLinkToLastBuild: false, keepAll: false, reportDir: 'bitbots_docs/docs/_out/',
                            reportFiles: 'index.html', reportName: 'Built Documentation', reportTitles: ''])
            }
        }

        stage('Deploy') {
            agent { label 'webserver' }
            when { branch 'master' }
            steps {
                unstash 'docs_output'
                deployDocs('bitbots_docs')
            }
        }
    }
}
